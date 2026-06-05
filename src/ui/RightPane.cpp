#include "RightPane.h"
#include "PluginListView.h"
#include "DataTab.h"
#include "ConflictsTab.h"
#include "DownloadsTab.h"
#include "core/AppConfig.h"
#include <QDir>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace solero {

RightPane::RightPane(QWidget* parent) : QTabWidget(parent) {
    m_pluginsTab   = new PluginListView(this);
    m_dataTab      = new DataTab(this);
    m_conflictsTab = new ConflictsTab(this);

    auto* pluginsContainer = new QWidget(this);
    auto* pluginsLayout = new QVBoxLayout(pluginsContainer);
    pluginsLayout->setContentsMargins(0, 0, 0, 0);
    pluginsLayout->setSpacing(0);
    m_pluginNotice = new QLabel(pluginsContainer);
    m_pluginNotice->setWordWrap(true);
    m_pluginNotice->setStyleSheet("background:#5a4a1e; color:#ffe08a; padding:4px;");
    m_pluginNotice->hide();
    pluginsLayout->addWidget(m_pluginNotice);
    pluginsLayout->addWidget(m_pluginsTab);

    addTab(pluginsContainer, "Plugins");
    addTab(m_dataTab,      "Data");
    addTab(m_conflictsTab, "Conflicts");
    m_downloadsTab = new DownloadsTab(this);
    addTab(m_downloadsTab, "Downloads");
}

void RightPane::showDownloadsTab() {
    setCurrentWidget(m_downloadsTab);
}

void RightPane::showPluginNotice(const QString& text) {
    m_pluginNotice->setText(text);
    m_pluginNotice->show();
}

void RightPane::hidePluginNotice() {
    m_pluginNotice->hide();
}

void RightPane::invalidateModPluginCache(const QString& id) {
    if (id.isEmpty()) m_modPluginCache.clear();
    else              m_modPluginCache.remove(id);
}

void RightPane::setProfile(Profile* profile, bool reconcilePlugins) {
    m_currentProfile = profile;
    // New profile -> different mods; drop the per-mod plugin highlight cache.
    m_modPluginCache.clear();
    if (reconcilePlugins)
        m_pluginsTab->reconcileWith(profile, AppConfig::instance().stagingDir());
    else
        m_pluginsTab->setProfile(profile); // bind model without the disk scan+save
    m_dataTab->setProfile(profile);
    m_downloadsTab->setProfile(profile);
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

    // Highlight selected mods' plugins in the Plugins tab. Cache each mod's Data
    // plugin filenames so repeated selections don't re-scan disk every time.
    QStringList pluginFiles;
    if (m_currentProfile) {
        for (const QString& id : ids) {
            if (id == "__separator__" || id == "__overwrite__") continue;
            auto it = m_modPluginCache.constFind(id);
            if (it == m_modPluginCache.constEnd()) {
                QString data = AppConfig::instance().stagingDir() + "/" + id + "/Data";
                QDir d(data);
                QStringList files = d.entryList({"*.esp","*.esm","*.esl"}, QDir::Files);
                it = m_modPluginCache.insert(id, files);
            }
            pluginFiles << it.value();
        }
    }
    m_pluginsTab->highlightPlugins(pluginFiles);
}

} // namespace solero
