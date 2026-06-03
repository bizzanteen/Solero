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

void RightPane::onSelectionChanged(const QStringList& ids) {
    m_dataTab->setSelection(ids);

    // Conflicts tab shows a single mod only; clear it otherwise.
    QStringList modIds;
    for (const auto& id : ids)
        if (id != "__separator__") modIds << id;
    m_conflictsTab->showMod(modIds.size() == 1 ? modIds.first() : QString());
}

} // namespace solero
