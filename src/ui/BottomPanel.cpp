#include "BottomPanel.h"
#include "LootRulesEditor.h"
#include <QLabel>

namespace solero {

BottomPanel::BottomPanel(QWidget* parent) : QTabWidget(parent) {
    addTab(new QLabel("Select a mod to see info", this), "Mod Info");
    addTab(new QLabel("Select a mod to see conflicts", this), "Conflicts");

    m_lootEditor = new LootRulesEditor(this);
    addTab(m_lootEditor, "LOOT Rules");

    setMaximumHeight(250);
}

void BottomPanel::setProfile(Profile* profile) {
    if (m_lootEditor) m_lootEditor->setProfile(profile);
}

} // namespace solero
