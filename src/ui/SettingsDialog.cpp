#include "SettingsDialog.h"
#include "ui/SetupPanel.h"
#include "core/AppConfig.h"
#include "app/NxmRegister.h"
#include "nexus/NexusApi.h"
#include "wabbajack/WabbajackEngine.h"
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

    // Disabled stub: deploy mode is not wired up yet.
    auto* deployRow = new QVBoxLayout;
    auto* deployLabel = new QLabel("Deploy mode (not wired up yet):", prefs);
    deployLabel->setEnabled(false);
    auto* deployCombo = new QComboBox(prefs);
    deployCombo->addItems({"Hard link", "Symlink", "Copy"});
    deployCombo->setEnabled(false);
    deployRow->addWidget(deployLabel);
    deployRow->addWidget(deployCombo);
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
        "Sign in with your Nexus personal API key. Click \"Get API Key\" to open "
        "your Nexus account page, copy the Personal API Key, and paste it below.",
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
        cfg.setJackifyEnginePath(m_jackifyEdit->text().trimmed());
        cfg.save();
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace solero
