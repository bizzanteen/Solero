#include "RightPane.h"
#include "PluginListView.h"
#include "DataTab.h"
#include "ConflictsTab.h"
#include "DownloadsTab.h"
#include "core/AppConfig.h"
#include <QDir>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
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

    // Top row with right-aligned "Backup LO" / "Restore LO…" / "LOOT Rules" /
    // "Sort Now" (LOOT) buttons.
    auto* sortRow = new QHBoxLayout();
    sortRow->setContentsMargins(4, 4, 4, 4);
    sortRow->addStretch();
    auto* backupBtn = new QPushButton("Backup LO", pluginsContainer);
    backupBtn->setToolTip("Snapshot the current load order + active state");
    connect(backupBtn, &QPushButton::clicked, this, &RightPane::backupLoRequested);
    sortRow->addWidget(backupBtn);
    auto* restoreBtn = new QPushButton("Restore LO\xe2\x80\xa6", pluginsContainer);
    restoreBtn->setToolTip("Restore a previously saved load-order snapshot");
    connect(restoreBtn, &QPushButton::clicked, this, &RightPane::restoreLoRequested);
    sortRow->addWidget(restoreBtn);
    auto* lootRulesBtn = new QPushButton("LOOT Rules", pluginsContainer);
    lootRulesBtn->setToolTip("Edit custom LOOT sorting rules");
    connect(lootRulesBtn, &QPushButton::clicked, this, &RightPane::lootRulesRequested);
    sortRow->addWidget(lootRulesBtn);
    m_lockBtn = new QPushButton("Lock Order", pluginsContainer);
    m_lockBtn->setCheckable(true);
    m_lockBtn->setToolTip("Lock the load order: skip LOOT auto-sort and keep the current manual order");
    connect(m_lockBtn, &QPushButton::toggled, this, &RightPane::lockOrderToggled);
    sortRow->addWidget(m_lockBtn);
    m_sortBtn = new QPushButton("Sort Now", pluginsContainer);
    m_sortBtn->setToolTip("Run LOOT to auto-sort the load order");
    m_sortBtn->setEnabled(false);
    connect(m_sortBtn, &QPushButton::clicked, this, &RightPane::sortRequested);
    sortRow->addWidget(m_sortBtn);
    pluginsLayout->addLayout(sortRow);

    pluginsLayout->addWidget(m_pluginsTab);

    addTab(pluginsContainer, "Plugins");
    addTab(m_dataTab,      "Data");
    addTab(m_conflictsTab, "Conflicts");
    m_downloadsTab = new DownloadsTab(this);
    addTab(m_downloadsTab, "Downloads");

    // Double-clicking a conflict row jumps to that mod's Data view.
    connect(m_conflictsTab, &ConflictsTab::fileActivated, this,
            [this](const QString& modId, const QString& /*relPath*/) {
        showDataFor(modId);
    });
    // Per-file rule changes (hide in Data tab, winner override in Conflicts tab)
    // bubble up so MainWindow can flag the deployment as dirty.
    connect(m_dataTab,      &DataTab::fileRulesChanged,      this, &RightPane::fileRulesChanged);
    connect(m_conflictsTab, &ConflictsTab::fileRulesChanged, this, &RightPane::fileRulesChanged);
}

void RightPane::showDownloadsTab() {
    setCurrentWidget(m_downloadsTab);
}

void RightPane::selectPlugin(const QString& filename) {
    if (auto* container = m_pluginsTab->parentWidget())
        setCurrentWidget(container);
    m_pluginsTab->selectPlugin(filename);
}

void RightPane::showPluginNotice(const QString& text) {
    m_pluginNotice->setText(text);
    m_pluginNotice->show();
}

void RightPane::hidePluginNotice() {
    m_pluginNotice->hide();
}

void RightPane::setSortButtonEnabled(bool enabled, const QString& tooltip) {
    if (!m_sortBtn) return;
    m_sortBtn->setEnabled(enabled);
    if (!tooltip.isEmpty()) m_sortBtn->setToolTip(tooltip);
}

void RightPane::setLockOrderChecked(bool checked) {
    if (!m_lockBtn) return;
    QSignalBlocker block(m_lockBtn); // reflect state without re-emitting toggled()
    m_lockBtn->setChecked(checked);
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
    m_conflictsTab->setProfile(profile); // for mod-id -> name resolution
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
