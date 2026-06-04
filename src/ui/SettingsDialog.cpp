#include "SettingsDialog.h"
#include "ui/SetupPanel.h"
#include "core/AppConfig.h"
#include "app/NxmRegister.h"
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>

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

    prefsLayout->addStretch();

    tabs->addTab(prefs, "Preferences");

    layout->addWidget(tabs);

    // Buttons
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(btns);

    connect(btns, &QDialogButtonBox::accepted, this, [this]{
        if (m_setupPanel->isValid()) m_setupPanel->save();
        auto& cfg = AppConfig::instance();
        cfg.setConfirmModDeletion(m_confirmDelete->isChecked());
        cfg.setCycleSeparatorColors(m_cycleSeparatorColors->isChecked());
        cfg.setDataShowAllFiles(m_dataShowAllFiles->isChecked());
        cfg.save();
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace solero
