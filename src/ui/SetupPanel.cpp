#include "SetupPanel.h"
#include "core/AppConfig.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QTemporaryFile>
#include <sys/stat.h>

namespace solero {

namespace {
// True if `child` is the same dir as, or nested inside, `parent`.
bool isInsideOrEqual(const QString& child, const QString& parent) {
    if (child.isEmpty() || parent.isEmpty()) return false;
    const QString c = QDir::cleanPath(QDir(child).absolutePath());
    const QString p = QDir::cleanPath(QDir(parent).absolutePath());
    if (c == p) return true;
    return c.startsWith(p + "/");
}

// True if `dir` can be created (if needed) and a temp file written into it.
bool dirIsWritable(const QString& dir) {
    if (dir.isEmpty()) return false;
    if (!QDir().mkpath(dir)) return false;
    QTemporaryFile probe(dir + "/.solero-write-test-XXXXXX");
    return probe.open();
}

// True if both dirs exist and live on the same filesystem (same st_dev). When
// staging and game differ, the hard-link deploy default silently degrades to
// copies (extra disk), so we warn. Returns true (no warning) if either can't be
// stat'd or the staging dir doesn't exist yet (created on save).
bool onSameFilesystem(const QString& a, const QString& b) {
    struct stat sa, sb;
    if (::stat(a.toLocal8Bit().constData(), &sa) != 0) return true;
    if (::stat(b.toLocal8Bit().constData(), &sb) != 0) return true;
    return sa.st_dev == sb.st_dev;
}
}

SetupPanel::SetupPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    // Header
    auto* header = new QLabel(
        "<h2>Welcome to Solero</h2>"
        "<p>Before we start, tell us where Skyrim Special Edition is installed "
        "and where you'd like to store your mods.</p>", this);
    header->setWordWrap(true);
    layout->addWidget(header);

    // Game directory
    auto* gameGroup = new QGroupBox("Skyrim SE Game Directory", this);
    auto* gameLayout = new QVBoxLayout(gameGroup);

    QStringList detected = AppConfig::detectSkyrimPaths();

    if (!detected.isEmpty()) {
        auto* detectedLabel = new QLabel("Detected installation(s):", gameGroup);
        m_gameDetectedCombo = new QComboBox(gameGroup);
        for (const auto& p : detected) m_gameDetectedCombo->addItem(p);
        m_gameDetectedCombo->addItem("Browse manually…");
        gameLayout->addWidget(detectedLabel);
        gameLayout->addWidget(m_gameDetectedCombo);
        connect(m_gameDetectedCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            if (text == "Browse manually…") {
                browseGameDir();
            } else {
                m_gameDirEdit->setText(text);
                refreshValidity();
            }
        });
    } else {
        // No Steam library yielded a Skyrim SE install - guide the user to browse.
        auto* noneHint = new QLabel(
            "No Skyrim Special Edition install was detected in your Steam libraries. "
            "Use Browse\xe2\x80\xa6 to locate the folder containing SkyrimSE.exe.", gameGroup);
        noneHint->setWordWrap(true);
        noneHint->setStyleSheet("color:#aaa;");
        gameLayout->addWidget(noneHint);
    }

    auto* gameRow = new QHBoxLayout;
    QString gameDefault = AppConfig::instance().gameDir();
    if (gameDefault.isEmpty() && !detected.isEmpty()) gameDefault = detected.first();
    m_gameDirEdit = new QLineEdit(gameDefault, gameGroup);
    m_gameDirEdit->setPlaceholderText("/path/to/Skyrim Special Edition");
    auto* gameBrowse = new QPushButton("Browse…", gameGroup);
    gameBrowse->setFixedWidth(80);
    gameRow->addWidget(m_gameDirEdit);
    gameRow->addWidget(gameBrowse);
    gameLayout->addLayout(gameRow);
    layout->addWidget(gameGroup);

    // Staging directory
    auto* stagingGroup = new QGroupBox("Mod Staging Directory", this);
    auto* stagingLayout = new QVBoxLayout(stagingGroup);

    auto* stagingHint = new QLabel(
        "This is where Solero stores installed mod files. "
        "Choose a location with plenty of free space.", stagingGroup);
    stagingHint->setWordWrap(true);
    stagingLayout->addWidget(stagingHint);

    auto* stagingRow = new QHBoxLayout;
    QString stagingDefault = AppConfig::instance().stagingDir();
    if (stagingDefault.isEmpty()) stagingDefault = QDir::homePath() + "/Modding/Solero/mods";
    m_stagingDirEdit = new QLineEdit(stagingDefault, stagingGroup);
    auto* stagingBrowse = new QPushButton("Browse…", stagingGroup);
    stagingBrowse->setFixedWidth(80);
    stagingRow->addWidget(m_stagingDirEdit);
    stagingRow->addWidget(stagingBrowse);
    stagingLayout->addLayout(stagingRow);
    layout->addWidget(stagingGroup);

    // Downloads directory
    auto* downloadsGroup = new QGroupBox("Downloads Directory", this);
    auto* downloadsLayout = new QVBoxLayout(downloadsGroup);

    auto* downloadsHint = new QLabel(
        "Folder where your downloaded mod archives live. "
        "Solero can install directly from here.", downloadsGroup);
    downloadsHint->setWordWrap(true);
    downloadsLayout->addWidget(downloadsHint);

    QString downloadsDefault = AppConfig::instance().downloadsDir();
    if (downloadsDefault.isEmpty()) {
        QDir d(m_stagingDirEdit->text().trimmed()); d.cdUp();
        downloadsDefault = d.absolutePath() + "/downloads";
    }

    auto* downloadsRow = new QHBoxLayout;
    m_downloadsEdit = new QLineEdit(downloadsDefault, downloadsGroup);
    auto* downloadsBrowse = new QPushButton("Browse…", downloadsGroup);
    downloadsBrowse->setFixedWidth(80);
    downloadsRow->addWidget(m_downloadsEdit);
    downloadsRow->addWidget(downloadsBrowse);
    downloadsLayout->addLayout(downloadsRow);
    layout->addWidget(downloadsGroup);

    // Status / validation
    m_statusLabel = new QLabel("", this);
    m_statusLabel->setStyleSheet("color: red;");
    layout->addWidget(m_statusLabel);

    connect(gameBrowse,     &QPushButton::clicked, this, &SetupPanel::browseGameDir);
    connect(stagingBrowse,  &QPushButton::clicked, this, &SetupPanel::browseStagingDir);
    connect(downloadsBrowse,&QPushButton::clicked, this, &SetupPanel::browseDownloadsDir);
    connect(m_gameDirEdit,   &QLineEdit::textChanged, this, &SetupPanel::refreshValidity);
    connect(m_stagingDirEdit,&QLineEdit::textChanged, this, &SetupPanel::refreshValidity);
    connect(m_downloadsEdit, &QLineEdit::textChanged, this, &SetupPanel::refreshValidity);

    refreshValidity();
}

void SetupPanel::browseGameDir() {
    QString path = QFileDialog::getExistingDirectory(
        this, "Select Skyrim SE Game Directory",
        QDir::homePath() + "/.local/share/Steam/steamapps/common");
    if (!path.isEmpty()) {
        m_gameDirEdit->setText(path);
        refreshValidity();
    }
}

void SetupPanel::browseStagingDir() {
    QString path = QFileDialog::getExistingDirectory(
        this, "Select Mod Staging Directory", QDir::homePath());
    if (!path.isEmpty())
        m_stagingDirEdit->setText(path);
}

void SetupPanel::browseDownloadsDir() {
    QString path = QFileDialog::getExistingDirectory(
        this, "Select Downloads Directory", QDir::homePath());
    if (!path.isEmpty())
        m_downloadsEdit->setText(path);
}

bool SetupPanel::isValid() const {
    QString gameDir = m_gameDirEdit->text().trimmed();
    bool gameOk = !gameDir.isEmpty() && QFile::exists(gameDir + "/SkyrimSE.exe");
    const QString stagingDir = m_stagingDirEdit->text().trimmed();
    if (!gameOk || stagingDir.isEmpty()) return false;
    // Staging must not be the game dir or live inside it (would deploy into
    // itself).
    if (isInsideOrEqual(stagingDir, gameDir)) return false;
    // Staging dir must be writable (Solero writes mod files there).
    if (!dirIsWritable(stagingDir)) return false;
    // Downloads dir must be writable.
    if (!dirIsWritable(m_downloadsEdit->text().trimmed())) return false;
    return true;
}

void SetupPanel::refreshValidity() {
    QString gameDir = m_gameDirEdit->text().trimmed();
    bool gameOk = !gameDir.isEmpty() && QFile::exists(gameDir + "/SkyrimSE.exe");
    const QString stagingDir = m_stagingDirEdit->text().trimmed();

    if (gameDir.isEmpty()) {
        m_statusLabel->setText("");
        m_statusLabel->setStyleSheet("color: red;");
    } else if (!gameOk) {
        m_statusLabel->setText("⚠ SkyrimSE.exe not found in that directory.");
        m_statusLabel->setStyleSheet("color: red;");
    } else if (isInsideOrEqual(stagingDir, gameDir)) {
        m_statusLabel->setText("⚠ Staging directory cannot be inside the game directory.");
        m_statusLabel->setStyleSheet("color: red;");
    } else if (!dirIsWritable(stagingDir)) {
        m_statusLabel->setText("⚠ Staging directory is not writable.");
        m_statusLabel->setStyleSheet("color: red;");
    } else if (!dirIsWritable(m_downloadsEdit->text().trimmed())) {
        m_statusLabel->setText("⚠ Downloads directory is not writable.");
        m_statusLabel->setStyleSheet("color: red;");
    } else if (!onSameFilesystem(stagingDir, gameDir)) {
        // Not a hard failure: deploy still works, but hard-links degrade to copies.
        m_statusLabel->setText("⚠ Staging and game dir are on different filesystems "
                               "- hard-link deploy will fall back to copies (extra disk).");
        m_statusLabel->setStyleSheet("color: #c8a000;");
    } else {
        m_statusLabel->setText("✓ Skyrim SE found.");
        m_statusLabel->setStyleSheet("color: green;");
    }

    emit validityChanged(isValid());
}

void SetupPanel::save() {
    auto& cfg = AppConfig::instance();
    cfg.setGameDir(m_gameDirEdit->text().trimmed());
    cfg.setStagingDir(m_stagingDirEdit->text().trimmed());

    // Create staging dir if it doesn't exist
    QDir().mkpath(cfg.stagingDir());

    cfg.setDownloadsDir(m_downloadsEdit->text().trimmed());
    QDir().mkpath(cfg.downloadsDir());

    cfg.save();
}

} // namespace solero
