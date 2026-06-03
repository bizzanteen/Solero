#include "BottomPanel.h"
#include "LootRulesEditor.h"
#include "bethini/IniEditorPanel.h"
#include <QLabel>

namespace solero {

BottomPanel::BottomPanel(QWidget* parent) : QTabWidget(parent) {
    addTab(new QLabel("Select a mod to see info", this), "Mod Info");
    addTab(new QLabel("Select a mod to see conflicts", this), "Conflicts");
    m_iniEditor = new IniEditorPanel(this);
    addTab(m_iniEditor, "INI Editor");

    m_lootEditor = new LootRulesEditor(this);
    addTab(m_lootEditor, "LOOT Rules");

    setMaximumHeight(250);
}

void BottomPanel::setProfile(Profile* profile) {
    if (m_lootEditor) m_lootEditor->setProfile(profile);
    if (m_iniEditor)  m_iniEditor->setProfile(profile);
}

} // namespace solero
