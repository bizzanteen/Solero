#include "SettingsDialog.h"
#include "ui/SetupPanel.h"
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

    // Preferences tab (stub)
    auto* prefs = new QWidget(tabs);
    auto* prefsLayout = new QVBoxLayout(prefs);

    auto* heading = new QLabel("<h3>Preferences (coming soon)</h3>", prefs);
    prefsLayout->addWidget(heading);

    auto* deployRow = new QVBoxLayout;
    auto* deployLabel = new QLabel("Deploy mode:", prefs);
    deployLabel->setEnabled(false);
    auto* deployCombo = new QComboBox(prefs);
    deployCombo->addItems({"Hard link", "Symlink", "Copy"});
    deployCombo->setEnabled(false);
    deployRow->addWidget(deployLabel);
    deployRow->addWidget(deployCombo);
    prefsLayout->addLayout(deployRow);

    auto* confirmDelete = new QCheckBox("Confirm before deleting mods", prefs);
    confirmDelete->setChecked(true);
    confirmDelete->setEnabled(false);
    prefsLayout->addWidget(confirmDelete);

    auto* followTheme = new QCheckBox("Follow system theme colours", prefs);
    followTheme->setChecked(true);
    followTheme->setEnabled(false);
    prefsLayout->addWidget(followTheme);

    auto* mo2Symlink = new QCheckBox("Default MO2 import to symlink", prefs);
    mo2Symlink->setEnabled(false);
    prefsLayout->addWidget(mo2Symlink);

    auto* checkUpdates = new QCheckBox("Check tools for updates on launch", prefs);
    checkUpdates->setEnabled(false);
    prefsLayout->addWidget(checkUpdates);

    auto* note = new QLabel("These aren't wired up yet.", prefs);
    note->setStyleSheet("color: gray; font-style: italic;");
    prefsLayout->addWidget(note);
    prefsLayout->addStretch();

    tabs->addTab(prefs, "Preferences");

    layout->addWidget(tabs);

    // Buttons
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(btns);

    connect(btns, &QDialogButtonBox::accepted, this, [this]{
        if (m_setupPanel->isValid()) m_setupPanel->save();
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace solero
