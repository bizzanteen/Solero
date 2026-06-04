#include "SettingsDialog.h"
#include "ui/SetupPanel.h"
#include "core/AppConfig.h"
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>

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
