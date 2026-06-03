#include "SetupWizard.h"
#include "core/AppConfig.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QGroupBox>

namespace solero {

SetupWizard::SetupWizard(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Welcome to Solero - Setup");
    setMinimumWidth(560);
    setModal(true);

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
                updateAcceptState();
            }
        });
    }

    auto* gameRow = new QHBoxLayout;
    m_gameDirEdit = new QLineEdit(detected.isEmpty() ? "" : detected.first(), gameGroup);
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
    m_stagingDirEdit = new QLineEdit(QDir::homePath() + "/Modding/Solero/mods", stagingGroup);
    auto* stagingBrowse = new QPushButton("Browse…", stagingGroup);
    stagingBrowse->setFixedWidth(80);
    stagingRow->addWidget(m_stagingDirEdit);
    stagingRow->addWidget(stagingBrowse);
    stagingLayout->addLayout(stagingRow);
    layout->addWidget(stagingGroup);

    // Status / validation
    m_statusLabel = new QLabel("", this);
    m_statusLabel->setStyleSheet("color: red;");
    layout->addWidget(m_statusLabel);

    // Buttons
    auto* btns = new QDialogButtonBox(this);
    m_acceptBtn = btns->addButton("Set Up Solero", QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(btns);

    connect(gameBrowse,   &QPushButton::clicked, this, &SetupWizard::browseGameDir);
    connect(stagingBrowse,&QPushButton::clicked, this, &SetupWizard::browseStagingDir);
    connect(m_gameDirEdit,   &QLineEdit::textChanged, this, &SetupWizard::updateAcceptState);
    connect(m_stagingDirEdit,&QLineEdit::textChanged, this, &SetupWizard::updateAcceptState);
    connect(btns, &QDialogButtonBox::accepted, this, &SetupWizard::onAccept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateAcceptState();
}

void SetupWizard::browseGameDir() {
    QString path = QFileDialog::getExistingDirectory(
        this, "Select Skyrim SE Game Directory",
        QDir::homePath() + "/.local/share/Steam/steamapps/common");
    if (!path.isEmpty()) {
        m_gameDirEdit->setText(path);
        updateAcceptState();
    }
}

void SetupWizard::browseStagingDir() {
    QString path = QFileDialog::getExistingDirectory(
        this, "Select Mod Staging Directory", QDir::homePath());
    if (!path.isEmpty())
        m_stagingDirEdit->setText(path);
}

void SetupWizard::updateAcceptState() {
    QString gameDir = m_gameDirEdit->text().trimmed();
    bool valid = !gameDir.isEmpty() && QFile::exists(gameDir + "/SkyrimSE.exe");

    if (gameDir.isEmpty()) {
        m_statusLabel->setText("");
    } else if (!valid) {
        m_statusLabel->setText("⚠ SkyrimSE.exe not found in that directory.");
    } else {
        m_statusLabel->setText("✓ Skyrim SE found.");
        m_statusLabel->setStyleSheet("color: green;");
    }

    if (m_acceptBtn) m_acceptBtn->setEnabled(valid && !m_stagingDirEdit->text().trimmed().isEmpty());
}

void SetupWizard::onAccept() {
    auto& cfg = AppConfig::instance();
    cfg.setGameDir(m_gameDirEdit->text().trimmed());
    cfg.setStagingDir(m_stagingDirEdit->text().trimmed());

    // Create staging dir if it doesn't exist
    QDir().mkpath(cfg.stagingDir());

    cfg.save();
    accept();
}

} // namespace solero
