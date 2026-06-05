#include "BottomPanel.h"
#include "LootRulesEditor.h"
#include "ModInfoWidget.h"
#include <QLabel>

namespace solero {

BottomPanel::BottomPanel(QWidget* parent) : QTabWidget(parent) {
    m_modInfo = new ModInfoWidget(this);
    addTab(m_modInfo, "Mod Info");
    addTab(new QLabel("Select a mod to see conflicts", this), "Conflicts");

    m_lootEditor = new LootRulesEditor(this);
    addTab(m_lootEditor, "LOOT Rules");

    setMaximumHeight(250);
}

void BottomPanel::setProfile(Profile* profile) {
    m_profile = profile;
    if (m_lootEditor) m_lootEditor->setProfile(profile);
    if (m_modInfo) m_modInfo->clear();
}

void BottomPanel::onModsSelected(const QStringList& ids) {
    if (m_modInfo)
        m_modInfo->showMod(m_profile, ids.isEmpty() ? QString() : ids.first());
}

} // namespace solero
