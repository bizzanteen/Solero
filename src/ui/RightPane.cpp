#include "RightPane.h"
#include "PluginListView.h"
#include "DataTab.h"
#include "ConflictsTab.h"

namespace solero {

RightPane::RightPane(QWidget* parent) : QTabWidget(parent) {
    m_pluginsTab   = new PluginListView(this);
    m_dataTab      = new DataTab(this);
    m_conflictsTab = new ConflictsTab(this);
    addTab(m_pluginsTab,   "Plugins");
    addTab(m_dataTab,      "Data");
    addTab(m_conflictsTab, "Conflicts");
}

void RightPane::setProfile(Profile* profile) {
    m_pluginsTab->setProfile(profile);
    m_dataTab->setProfile(profile);
}

void RightPane::setConflictIndex(const ConflictIndex& index) {
    m_conflictIndex = index;
    m_conflictsTab->setConflictIndex(index);
    m_dataTab->setConflictIndex(index);
}

void RightPane::onModSelected(const QString& modId) {
    m_dataTab->showMod(modId);
    m_conflictsTab->showMod(modId);
}

} // namespace solero
