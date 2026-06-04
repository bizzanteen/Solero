#include "RightPane.h"
#include "PluginListView.h"
#include "DataTab.h"
#include "ConflictsTab.h"
#include "DownloadsTab.h"
#include "core/AppConfig.h"
#include <QDir>

namespace solero {

RightPane::RightPane(QWidget* parent) : QTabWidget(parent) {
    m_pluginsTab   = new PluginListView(this);
    m_dataTab      = new DataTab(this);
    m_conflictsTab = new ConflictsTab(this);
    addTab(m_pluginsTab,   "Plugins");
    addTab(m_dataTab,      "Data");
    addTab(m_conflictsTab, "Conflicts");
    m_downloadsTab = new DownloadsTab(this);
    addTab(m_downloadsTab, "Downloads");
}

void RightPane::setProfile(Profile* profile) {
    m_currentProfile = profile;
    m_pluginsTab->reconcileWith(profile, AppConfig::instance().stagingDir());
    m_dataTab->setProfile(profile);
}

void RightPane::refreshPlugins(Profile* profile) {
    m_pluginsTab->reconcileWith(profile, AppConfig::instance().stagingDir());
}

void RightPane::setConflictIndex(const ConflictIndex& index) {
    m_conflictIndex = index;
    m_conflictsTab->setConflictIndex(index);
    m_dataTab->setConflictIndex(index);
}

void RightPane::showDataFor(const QString& modId) {
    m_dataTab->setSelection({modId});
    setCurrentWidget(m_dataTab);
}

void RightPane::onSelectionChanged(const QStringList& ids) {
    m_dataTab->setSelection(ids);

    // Conflicts tab shows a single mod only; clear it otherwise.
    QStringList modIds;
    for (const auto& id : ids)
        if (id != "__separator__") modIds << id;
    m_conflictsTab->showMod(modIds.size() == 1 ? modIds.first() : QString());

    // Highlight selected mods' plugins in the Plugins tab.
    QStringList pluginFiles;
    if (m_currentProfile) {
        for (const QString& id : ids) {
            if (id == "__separator__" || id == "__overwrite__") continue;
            QString data = AppConfig::instance().stagingDir() + "/" + id + "/Data";
            QDir d(data);
            for (const QString& f : d.entryList({"*.esp","*.esm","*.esl"}, QDir::Files))
                pluginFiles << f;
        }
    }
    m_pluginsTab->highlightPlugins(pluginFiles);
}

} // namespace solero
