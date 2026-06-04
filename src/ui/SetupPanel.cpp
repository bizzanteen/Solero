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
#include <QGroupBox>

namespace solero {

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
    return gameOk && !m_stagingDirEdit->text().trimmed().isEmpty();
}

void SetupPanel::refreshValidity() {
    QString gameDir = m_gameDirEdit->text().trimmed();
    bool gameOk = !gameDir.isEmpty() && QFile::exists(gameDir + "/SkyrimSE.exe");

    if (gameDir.isEmpty()) {
        m_statusLabel->setText("");
        m_statusLabel->setStyleSheet("color: red;");
    } else if (!gameOk) {
        m_statusLabel->setText("⚠ SkyrimSE.exe not found in that directory.");
        m_statusLabel->setStyleSheet("color: red;");
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
