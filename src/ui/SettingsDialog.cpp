#include "SettingsDialog.h"
#include "ui/SetupPanel.h"
#include "core/AppConfig.h"
#include "app/NxmRegister.h"
#include "nexus/NexusApi.h"
#include "wabbajack/WabbajackEngine.h"
#include "deploy/DeployMode.h"
#include <QGroupBox>
#include <QFileDialog>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QDir>
#include <QClipboard>
#include <QGuiApplication>

namespace solero {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Settings");
    setMinimumWidth(560);
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);

    // Setup tab
    m_setupPanel = new SetupPanel(tabs);
    tabs->addTab(m_setupPanel, "Setup");

    // Preferences tab
    auto& cfg = AppConfig::instance();
    auto* prefs = new QWidget(tabs);
    auto* prefsLayout = new QVBoxLayout(prefs);

    auto* heading = new QLabel("<h3>Preferences</h3>", prefs);
    prefsLayout->addWidget(heading);

    m_confirmDelete = new QCheckBox("Confirm before deleting mods", prefs);
    m_confirmDelete->setChecked(cfg.confirmModDeletion());
    prefsLayout->addWidget(m_confirmDelete);

    m_cycleSeparatorColors = new QCheckBox("Automatically give new separators a different colour", prefs);
    m_cycleSeparatorColors->setChecked(cfg.cycleSeparatorColors());
    prefsLayout->addWidget(m_cycleSeparatorColors);

    m_dataShowAllFiles = new QCheckBox("Data tab: show all files by default", prefs);
    m_dataShowAllFiles->setChecked(cfg.dataShowAllFiles());
    prefsLayout->addWidget(m_dataShowAllFiles);

    m_promptAfterBrowserDownload = new QCheckBox(
        "Ask whether to view Downloads after starting a browser download", prefs);
    m_promptAfterBrowserDownload->setChecked(cfg.promptAfterBrowserDownload());
    prefsLayout->addWidget(m_promptAfterBrowserDownload);

    m_autoCheckUpdates = new QCheckBox(
        "Automatically check Nexus for mod updates (every few hours)", prefs);
    m_autoCheckUpdates->setChecked(cfg.autoCheckUpdates());
    prefsLayout->addWidget(m_autoCheckUpdates);

    // Nexus nxm:// handler registration (opt-in, user-initiated).
    m_nxmStatus = new QLabel(NxmRegister::isRegistered()
        ? "Registered as the nxm:// handler"
        : "Not registered", prefs);
    prefsLayout->addWidget(m_nxmStatus);

    auto* nxmBtn = new QPushButton("Register as Nexus download handler (nxm://)", prefs);
    prefsLayout->addWidget(nxmBtn);
    connect(nxmBtn, &QPushButton::clicked, this, [this]{
        QString msg;
        bool ok = solero::NxmRegister::registerHandler(msg);
        m_nxmStatus->setText(solero::NxmRegister::isRegistered()
            ? "Registered as the nxm:// handler"
            : "Not registered");
        QMessageBox::information(this, "Nexus Handler", ok
            ? "Solero is now registered for Nexus \"Mod Manager Download\" links.\n\n" + msg
            : ("Registration failed:\n" + msg));
    });

    // Deploy mode: how staged mod files are projected into the game's Data dir.
    auto* deployRow = new QVBoxLayout;
    deployRow->addWidget(new QLabel("Deploy mode:", prefs));
    m_deployCombo = new QComboBox(prefs);
    m_deployCombo->addItems({"Hard link", "Symlink", "Copy"}); // index == DeployMode
    switch (cfg.deployMode()) {
        case DeployMode::SymLink: m_deployCombo->setCurrentIndex(1); break;
        case DeployMode::Copy:    m_deployCombo->setCurrentIndex(2); break;
        default:                  m_deployCombo->setCurrentIndex(0); break;
    }
    deployRow->addWidget(m_deployCombo);
    auto* deployHelp = new QLabel(
        "Hard link: same filesystem only, instant, no extra disk. "
        "Symlink: works across filesystems. "
        "Copy: works anywhere but duplicates files on disk.", prefs);
    deployHelp->setWordWrap(true);
    deployHelp->setStyleSheet("color:#888;");
    deployRow->addWidget(deployHelp);
    prefsLayout->addLayout(deployRow);

    // Wabbajack group
    auto* wjGroup = new QGroupBox("Wabbajack", prefs);
    auto* wjLayout = new QVBoxLayout(wjGroup);
    wjLayout->addWidget(new QLabel("jackify-engine path", wjGroup));
    auto* wjRow = new QHBoxLayout;
    m_jackifyEdit = new QLineEdit(cfg.jackifyEnginePath(), wjGroup);
    m_jackifyEdit->setPlaceholderText("(auto-detect)");
    auto* wjBrowse = new QPushButton("Browse\xe2\x80\xa6", wjGroup);
    wjRow->addWidget(m_jackifyEdit, 1);
    wjRow->addWidget(wjBrowse);
    wjLayout->addLayout(wjRow);
    const QString detected = WabbajackEngine::findEngine();
    auto* wjDetected = new QLabel(
        detected.isEmpty() ? "(not found)" : ("(auto-detected: " + detected + ")"), wjGroup);
    wjDetected->setStyleSheet("color:#888;");
    wjDetected->setVisible(cfg.jackifyEnginePath().isEmpty());
    wjLayout->addWidget(wjDetected);
    connect(wjBrowse, &QPushButton::clicked, this, [this, wjDetected]{
        const QString p = QFileDialog::getOpenFileName(this, "Locate jackify-engine", QDir::homePath());
        if (!p.isEmpty()) { m_jackifyEdit->setText(p); wjDetected->setVisible(false); }
    });
    connect(m_jackifyEdit, &QLineEdit::textChanged, this, [wjDetected](const QString& t){
        wjDetected->setVisible(t.trimmed().isEmpty());
    });
    prefsLayout->addWidget(wjGroup);

    prefsLayout->addStretch();

    tabs->addTab(prefs, "Preferences");

    // Nexus Account tab
    auto* nexus = new QWidget(tabs);
    auto* nexusLayout = new QVBoxLayout(nexus);

    auto* nexusHeading = new QLabel("<h3>Nexus Account</h3>", nexus);
    nexusLayout->addWidget(nexusHeading);

    auto* nexusHelp = new QLabel(
        QStringLiteral("Connect to Nexus ") + QChar(0x2192)
            + QStringLiteral(" sign in, copy your Personal API Key, then "
              "\"Paste key & connect\". This opens the built-in Nexus browser straight "
              "to your API-key page (you're usually already signed in). You can still "
              "paste a key manually below if you prefer."),
        nexus);
    nexusHelp->setWordWrap(true);
    nexusLayout->addWidget(nexusHelp);

    m_nexusStatus = new QLabel(nexus);
    nexusLayout->addWidget(m_nexusStatus);

    auto applyStatus = [this](const NexusApi::UserInfo& info) {
        if (info.ok) {
            const QString tier = info.premium ? "Premium" : "Supporter/Free";
            m_nexusStatus->setText(QString("<span style=\"color:green;\">Signed in as %1  (%2)</span>")
                                       .arg(info.name.toHtmlEscaped(), tier));
            if (m_signOutBtn) m_signOutBtn->setEnabled(true);
        } else {
            m_nexusStatus->setText("Not signed in.");
            if (m_signOutBtn) m_signOutBtn->setEnabled(false);
        }
    };

    m_keyEdit = new QLineEdit(nexus);
    m_keyEdit->setEchoMode(QLineEdit::Password);
    m_keyEdit->setPlaceholderText("Paste your Nexus API key…");
    nexusLayout->addWidget(m_keyEdit);

    // Primary one-click flow: open the embedded browser at the API-key page,
    // then paste the copied key back here without hunting for the field.
    auto* connectRow = new QHBoxLayout;
    auto* connectBtn = new QPushButton(
        QStringLiteral("Connect to Nexus ") + QChar(0x2192), nexus);
    connectBtn->setToolTip(QStringLiteral(
        "Open the built-in Nexus browser at your API-key page"));
    auto* pasteBtn = new QPushButton("Paste key & connect", nexus);
    pasteBtn->setToolTip(QStringLiteral(
        "Read the API key you copied from the page and sign in"));
    connectRow->addWidget(connectBtn);
    connectRow->addWidget(pasteBtn);
    connectRow->addStretch();
    nexusLayout->addLayout(connectRow);

    auto* nexusBtnRow = new QHBoxLayout;
    auto* getKeyBtn = new QPushButton("Get API Key", nexus);
    auto* signInBtn = new QPushButton("Sign In", nexus);
    m_signOutBtn = new QPushButton("Sign Out", nexus);
    nexusBtnRow->addWidget(getKeyBtn);
    nexusBtnRow->addWidget(signInBtn);
    nexusBtnRow->addWidget(m_signOutBtn);
    nexusBtnRow->addStretch();
    nexusLayout->addLayout(nexusBtnRow);

    nexusLayout->addStretch();

    // Only validate on open if a key file already exists, to avoid any blocking
    // network call when the user has never signed in.
    if (QFile::exists(NexusApi::apiKeyPath()))
        applyStatus(NexusApi::validateUser());
    else
        applyStatus({});

    connect(getKeyBtn, &QPushButton::clicked, this, []{
        QDesktopServices::openUrl(QUrl("https://www.nexusmods.com/users/myaccount?tab=api"));
    });

    // "Connect to Nexus": close Settings and let MainWindow open the embedded
    // browser at the API-key page. accept() keeps the user's other edits.
    connect(connectBtn, &QPushButton::clicked, this, [this]{
        emit connectNexusRequested();
        accept();
    });

    // "Paste key & connect": grab the clipboard, sanity-check it, validate via
    // the Nexus API, store it, and report inline - no field hunting required.
    connect(pasteBtn, &QPushButton::clicked, this, [this, applyStatus]{
        const QString key = QGuiApplication::clipboard()->text().trimmed();
        const bool looksLikeKey = !key.isEmpty()
            && !key.contains(QStringLiteral("://"))
            && !key.contains(QLatin1Char('<'))
            && !key.contains(QLatin1Char(' '))
            && !key.contains(QLatin1Char('\n'));
        const QString badKeyMsg =
            QStringLiteral("That clipboard text isn't a valid Nexus API key ") + QChar('-')
            + QStringLiteral(" copy the Personal API Key from the page and try again.");
        if (!looksLikeKey) {
            QMessageBox::warning(this, "Nexus Account", badKeyMsg);
            return;
        }
        const auto info = NexusApi::validateUser(key);
        if (!info.ok) {
            QMessageBox::warning(this, "Nexus Account", badKeyMsg);
            return;
        }
        NexusApi::setApiKey(key);
        applyStatus(info);
        m_keyEdit->clear();
        QMessageBox::information(this, "Nexus Account",
                                 QString("Signed in as %1.").arg(info.name));
    });

    connect(signInBtn, &QPushButton::clicked, this, [this, applyStatus]{
        const QString key = m_keyEdit->text().trimmed();
        if (key.isEmpty()) {
            QMessageBox::warning(this, "Nexus Account", "Paste your Nexus API key first.");
            return;
        }
        const auto info = NexusApi::validateUser(key);
        if (info.ok) {
            NexusApi::setApiKey(key);
            applyStatus(info);
            m_keyEdit->clear();
            QMessageBox::information(this, "Nexus Account",
                                     QString("Signed in as %1.").arg(info.name));
        } else {
            QMessageBox::warning(this, "Nexus Account",
                "That API key didn't validate - check you copied the Personal API Key correctly.");
        }
    });

    connect(m_signOutBtn, &QPushButton::clicked, this, [this, applyStatus]{
        NexusApi::clearApiKey();
        applyStatus({});
        QMessageBox::information(this, "Nexus Account", "Signed out.");
    });

    tabs->addTab(nexus, "Nexus Account");

    layout->addWidget(tabs);

    // Buttons
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(btns);

    connect(btns, &QDialogButtonBox::accepted, this, [this]{
        if (m_setupPanel->isValid()) {
            m_setupPanel->save();
        } else {
            // Don't silently discard the user's path edits - tell them they
            // weren't saved, then still persist preferences and close.
            QMessageBox::warning(this, "Game Paths Not Saved",
                "The game/staging/downloads paths are invalid, so those changes "
                "were not saved. Check the Setup tab.\n\n"
                "Your other preference changes have been saved.");
        }
        auto& cfg = AppConfig::instance();
        cfg.setConfirmModDeletion(m_confirmDelete->isChecked());
        cfg.setCycleSeparatorColors(m_cycleSeparatorColors->isChecked());
        cfg.setDataShowAllFiles(m_dataShowAllFiles->isChecked());
        cfg.setPromptAfterBrowserDownload(m_promptAfterBrowserDownload->isChecked());
        cfg.setAutoCheckUpdates(m_autoCheckUpdates->isChecked());
        switch (m_deployCombo->currentIndex()) {
            case 1:  cfg.setDeployMode(DeployMode::SymLink); break;
            case 2:  cfg.setDeployMode(DeployMode::Copy);    break;
            default: cfg.setDeployMode(DeployMode::HardLink); break;
        }
        cfg.setJackifyEnginePath(m_jackifyEdit->text().trimmed());
        cfg.save();
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace solero
