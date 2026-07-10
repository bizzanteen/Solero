#include "RightPane.h"
#include "PluginListView.h"
#include "DataTab.h"
#include "ConflictsTab.h"
#include "DownloadsTab.h"
#include "SavesTab.h"
#include "core/AppConfig.h"
#include "core/PluginOrigin.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QAction>
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

    // Top row: the plugin tally + a single "Load Order" dropdown on the left, with
    // the search box as the rightmost element (filling the remaining width).
    auto* sortRow = new QHBoxLayout();
    sortRow->setContentsMargins(4, 4, 4, 4);
    // Enabled-plugin tally against the two hard caps (full plugins / 254 load-order
    // slots, light plugins / 4096 ESL slots). Sits at the row's left edge; kept
    // live by updatePluginCounts().
    m_pluginCountLabel = new QLabel(pluginsContainer);
    m_pluginCountLabel->setToolTip(
        QStringLiteral("Enabled plugins vs their caps:\n"
                       "ESP = full plugins (ESM + ESP) using a load-order slot (max 254)\n"
                       "ESL = light / ESL-flagged plugins (max 4096)"));
    sortRow->addWidget(m_pluginCountLabel);

    // Every load-order action lives in one "Load Order" dropdown - the per-session
    // Sort Now + Lock Order (checkable) join Backup / Restore / LOOT Rules here so
    // the toolbar stays a single button. Sort Now / Lock Order are kept as members
    // so setSortButtonEnabled() / setLockOrderChecked() can drive them.
    auto* loMenuBtn = new QPushButton("Load Order", pluginsContainer);
    loMenuBtn->setToolTip("Sort, lock, back up, restore, and LOOT-rule actions for the load order");
    auto* loMenu = new QMenu(loMenuBtn);
    m_sortAction = loMenu->addAction("Sort Now", this, &RightPane::sortRequested);
    m_sortAction->setToolTip("Run LOOT to auto-sort the load order");
    m_sortAction->setEnabled(false);
    m_lockAction = loMenu->addAction("Lock Order");
    m_lockAction->setCheckable(true);
    m_lockAction->setToolTip("Lock the load order: skip LOOT auto-sort and keep the current manual order");
    connect(m_lockAction, &QAction::toggled, this, &RightPane::lockOrderToggled);
    loMenu->addSeparator();
    loMenu->addAction("Backup Load Order", this, &RightPane::backupLoRequested)
        ->setToolTip("Snapshot the current load order + active state");
    loMenu->addAction(QStringLiteral("Restore Load Order") + QChar(0x2026),
                      this, &RightPane::restoreLoRequested)
        ->setToolTip("Restore a previously saved load-order snapshot");
    loMenu->addAction(QStringLiteral("LOOT Rules") + QChar(0x2026),
                      this, &RightPane::lootRulesRequested)
        ->setToolTip("Edit custom LOOT sorting rules");
    loMenu->setToolTipsVisible(true);
    loMenuBtn->setMenu(loMenu); // QPushButton draws its own dropdown indicator
    sortRow->addWidget(loMenuBtn);

    m_pluginSearch = new QLineEdit(pluginsContainer);
    m_pluginSearch->setPlaceholderText(QStringLiteral("Search plugins") + QChar(0x2026));
    m_pluginSearch->setClearButtonEnabled(true);
    sortRow->addWidget(m_pluginSearch, 1); // rightmost element, fills the remaining width
    pluginsLayout->addLayout(sortRow);

    // Debounce the search input (mirrors the Data tab): filter once typing pauses.
    m_pluginSearchDebounce = new QTimer(this);
    m_pluginSearchDebounce->setSingleShot(true);
    m_pluginSearchDebounce->setInterval(200);
    connect(m_pluginSearchDebounce, &QTimer::timeout, this, [this] {
        m_pluginsTab->setFilter(m_pluginSearch->text());
    });
    connect(m_pluginSearch, &QLineEdit::textChanged, this,
            [this](const QString&) { m_pluginSearchDebounce->start(); });

    pluginsLayout->addWidget(m_pluginsTab);

    addTab(pluginsContainer, "Plugins");
    addTab(m_dataTab,      "Data");
    addTab(m_conflictsTab, "Conflicts");
    m_downloadsTab = new DownloadsTab(this);
    addTab(m_downloadsTab, "Downloads");
    m_savesTab = new SavesTab(this);
    addTab(m_savesTab, "Saves");

    // Double-clicking a conflict row jumps to that mod's Data view.
    connect(m_conflictsTab, &ConflictsTab::fileActivated, this,
            [this](const QString& modId, const QString& /*relPath*/) {
        showDataFor(modId);
    });
    // Per-file rule changes (hide in Data tab, winner override in Conflicts tab)
    // bubble up so MainWindow can flag the deployment as dirty.
    connect(m_dataTab,      &DataTab::fileRulesChanged,      this, &RightPane::fileRulesChanged);
    connect(m_conflictsTab, &ConflictsTab::fileRulesChanged, this, &RightPane::fileRulesChanged);
    connect(m_dataTab,      &DataTab::renameRequested,       this, &RightPane::renameRequested);
    connect(m_dataTab,      &DataTab::deleteRequested,       this, &RightPane::deleteRequested);
    connect(m_pluginsTab, &PluginListView::pluginClicked,
            this, &RightPane::onPluginClicked);
    connect(m_pluginsTab, &PluginListView::pluginActivated,
            this, &RightPane::onPluginActivated);
    // Keep the ESP/ESL tally live: enable toggles + ESL-flag edits both fire
    // pluginEnabledChanged; a reorder can't change counts but is cheap to refresh.
    connect(m_pluginsTab, &PluginListView::pluginEnabledChanged,
            this, &RightPane::updatePluginCounts);
    connect(m_pluginsTab, &PluginListView::loadOrderChanged,
            this, &RightPane::updatePluginCounts);
    updatePluginCounts(); // seed the (empty) label before a profile is bound
}

void RightPane::showDownloadsTab() {
    setCurrentWidget(m_downloadsTab);
}

void RightPane::selectPlugin(const QString& filename) {
    if (auto* container = m_pluginsTab->parentWidget())
        setCurrentWidget(container);
    m_pluginsTab->selectPlugin(filename);
}

void RightPane::focusPluginSearch() {
    if (auto* container = m_pluginsTab->parentWidget())
        setCurrentWidget(container);
    m_pluginSearch->setFocus(Qt::ShortcutFocusReason);
    m_pluginSearch->selectAll();
}

void RightPane::showPluginNotice(const QString& text) {
    m_pluginNotice->setText(text);
    m_pluginNotice->show();
}

void RightPane::hidePluginNotice() {
    m_pluginNotice->hide();
}

void RightPane::setSortButtonEnabled(bool enabled, const QString& tooltip) {
    if (!m_sortAction) return;
    m_sortAction->setEnabled(enabled);
    if (!tooltip.isEmpty()) m_sortAction->setToolTip(tooltip);
}

void RightPane::setLockOrderChecked(bool checked) {
    if (!m_lockAction) return;
    QSignalBlocker block(m_lockAction); // reflect state without re-emitting toggled()
    m_lockAction->setChecked(checked);
}

void RightPane::invalidateModPluginCache(const QString& id) {
    if (id.isEmpty()) m_modPluginCache.clear();
    else              m_modPluginCache.remove(id);
    m_pluginOriginCache.clear();
    m_originIndexBuilt = false;
}

void RightPane::setProfile(Profile* profile, bool reconcilePlugins) {
    m_currentProfile = profile;
    // New profile -> different mods; drop the per-mod plugin highlight cache.
    m_modPluginCache.clear();
    m_pluginOriginCache.clear();
    m_originIndexBuilt = false;
    if (reconcilePlugins)
        m_pluginsTab->reconcileWith(profile, AppConfig::instance().stagingDir());
    else
        m_pluginsTab->setProfile(profile); // bind model without the disk scan+save
    m_dataTab->setProfile(profile);
    m_conflictsTab->setProfile(profile); // for mod-id -> name resolution
    m_downloadsTab->setProfile(profile);
    if (m_savesTab) m_savesTab->setProfile(profile); // read-only saves + missing-plugin flag
    updatePluginCounts();
}

// ESP = enabled full plugins (ESM + ESP occupying a 00..FD load-order slot, cap
// 254); ESL = enabled light / ESL-flagged plugins (FE xxx slots, cap 4096). The
// counts turn amber as they near a cap and red once over it, mirroring the
// plugin-limit health check.
void RightPane::updatePluginCounts() {
    if (!m_pluginCountLabel) return;
    int esp = 0, esl = 0;
    if (m_currentProfile) {
        const PluginList& pl = m_currentProfile->pluginList();
        for (int i = 0; i < pl.count(); ++i) {
            const PluginEntry& p = pl.at(i);
            if (!p.enabled) continue;
            const bool light = p.isLight
                            || p.filename.endsWith(QStringLiteral(".esl"), Qt::CaseInsensitive);
            (light ? esl : esp)++;
        }
    }
    constexpr int kEspCap = 254, kEslCap = 4096;
    // Colour a count red at/over its cap, amber when close, else follow the theme.
    auto tint = [](int n, int cap) -> QString {
        if (n >= cap)            return QStringLiteral("<span style='color:#D64550'>");
        if (n >= cap - 14)       return QStringLiteral("<span style='color:#F2C230'>");
        return QString();
    };
    const QString espSpan = tint(esp, kEspCap), eslSpan = tint(esl, kEslCap);
    const QString close = QStringLiteral("</span>");
    m_pluginCountLabel->setText(
        QStringLiteral("ESP %1%2/%3%4 · ESL %5%6/%7%8")
            .arg(espSpan).arg(esp).arg(kEspCap).arg(espSpan.isEmpty() ? QString() : close)
            .arg(eslSpan).arg(esl).arg(kEslCap).arg(eslSpan.isEmpty() ? QString() : close));
}

void RightPane::refreshPlugins(Profile* profile) {
    // The origin index depends on mod order + enabled state, both of which can
    // change via a reorder / enable-disable that lands here - invalidate it so the
    // next plugin click rebuilds against the current order. (Only the flag + the
    // derived index; the per-mod filename cache is order/enabled-independent.)
    m_originIndexBuilt = false;
    m_pluginOriginCache.clear();
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

// True if the mod's staging root holds at least one deployable LOOSE file - i.e.
// any real file that is not a plugin (.esp/.esm/.esl, shown in the Plugins tab)
// and not per-mod metadata (.solero* markers, legacy fomod-choices.json). When
// false, the Data tab would be empty/useless for this mod, so the caller can
// auto-switch to the more useful Plugins tab. Mirrors DeployEngine's skip rules.
bool RightPane::modHasLooseData(const QString& modId) const {
    if (!m_currentProfile) return false;
    const QString folder = m_currentProfile->stagingFolderFor(modId);
    if (folder.isEmpty()) return false;
    const QString modRoot = AppConfig::instance().stagingDir() + "/" + folder;
    QDir dir(modRoot);
    if (!dir.exists()) return false;

    // Cheap, NON-recursive top-level check. This runs on every selection, so it
    // must not walk the whole staging tree - a recursive walk here froze the UI
    // for large mods. A subdirectory at the top level (textures/, meshes/, …)
    // means loose data; a top-level non-plugin, non-metadata file also counts.
    // Plugin-only mods have just an .esp/.esm/.esl at the root -> reported as no
    // loose data, so the caller lands the user on the Plugins tab.
    const auto entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        const QString fn = fi.fileName();
        if (fn.startsWith(".solero")
            || fn.compare("fomod-choices.json", Qt::CaseInsensitive) == 0)
            continue; // per-mod metadata, never deployed
        if (fi.isDir()) return true; // a data subtree (textures/, meshes/, scripts/, …)
        if (fn.endsWith(".esp", Qt::CaseInsensitive)
            || fn.endsWith(".esm", Qt::CaseInsensitive)
            || fn.endsWith(".esl", Qt::CaseInsensitive))
            continue; // plugins live in the Plugins tab, not the Data view
        return true; // a loose non-plugin file at the root
    }
    return false;
}

// Of the given plugin filenames, return the one that appears EARLIEST in the
// current load order (the profile's PluginList), or empty if none are present.
QString RightPane::firstPluginInLoadOrder(const QStringList& filenames) const {
    if (!m_currentProfile || filenames.isEmpty()) return QString();
    const PluginList& pl = m_currentProfile->pluginList();
    int bestIdx = -1;
    QString best;
    for (const QString& fn : filenames) {
        for (int i = 0; i < pl.count(); ++i) {
            if (pl.at(i).filename.compare(fn, Qt::CaseInsensitive) == 0) {
                if (bestIdx < 0 || i < bestIdx) { bestIdx = i; best = fn; }
                break;
            }
        }
    }
    return best;
}

void RightPane::onSelectionChanged(const QStringList& ids) {
    emit highlightOriginMods({}); // a mod selection supersedes any plugin-origin highlight
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
                QString data = AppConfig::instance().stagingDir() + "/"
                             + m_currentProfile->stagingFolderFor(id) + "/Data";
                QDir d(data);
                QStringList files = d.entryList({"*.esp","*.esm","*.esl"}, QDir::Files);
                it = m_modPluginCache.insert(id, files);
            }
            pluginFiles << it.value();
        }
    }
    m_pluginsTab->highlightPlugins(pluginFiles);

    // Single-mod selection drives two view conveniences (multi-select keeps the
    // current tab untouched so the user isn't yanked around).
    QWidget* pluginsContainer = m_pluginsTab->parentWidget();
    if (modIds.size() == 1) {
        const QString modId = modIds.first();
        // TASK 3: a mod with no loose Data-folder content would show an empty
        // Data tab - switch to the more useful Plugins tab instead. (A plugin-
        // only mod has no loose data, so this lands the user on Plugins, where
        // its ESPs live.)
        if (modId != "__overwrite__" && !modHasLooseData(modId)
            && currentWidget() == m_dataTab) {
            if (pluginsContainer) setCurrentWidget(pluginsContainer);
        }

        // TASK 4: when the Plugins tab is the active view, scroll the load order
        // to this mod's first plugin (earliest in load order) and select it, so
        // the user can see where its plugins sit. selectPlugin() switches-to +
        // selects + scrolls; the tab-switch above may have made Plugins current.
        if (pluginsContainer && currentWidget() == pluginsContainer) {
            const QString first = firstPluginInLoadOrder(pluginFiles);
            if (!first.isEmpty()) selectPlugin(first);
        }
    }
}

void RightPane::ensurePluginOriginIndex() {
    if (m_originIndexBuilt || !m_currentProfile) return;
    QStringList ordered;                       // enabled mods, list order (low->high)
    QHash<QString, QStringList> byMod;
    const ModList& ml = m_currentProfile->modList();
    for (int i = 0; i < ml.count(); ++i) {
        const auto& e = ml.at(i);
        if (e.type != EntryType::Mod || !e.enabled) continue;
        ordered << e.id;
        auto it = m_modPluginCache.constFind(e.id);
        if (it == m_modPluginCache.constEnd()) {
            const QString data = AppConfig::instance().stagingDir() + "/"
                               + m_currentProfile->stagingFolderFor(e.id) + "/Data";
            it = m_modPluginCache.insert(e.id,
                     QDir(data).entryList({"*.esp","*.esm","*.esl"}, QDir::Files));
        }
        byMod.insert(e.id, it.value());
    }
    m_pluginOriginCache = PluginOrigin::buildIndex(ordered, byMod);
    m_originIndexBuilt = true;
}

void RightPane::onPluginClicked(const QString& filename) {
    ensurePluginOriginIndex();
    const QStringList providers = m_pluginOriginCache.value(filename.toLower());
    QHash<QString,int> roles;
    for (int i = 0; i < providers.size(); ++i)
        roles.insert(providers.at(i), i == providers.size() - 1 ? 1 : 2); // last = winner
    emit highlightOriginMods(roles);
}

void RightPane::onPluginActivated(const QString& filename) {
    ensurePluginOriginIndex();
    const QString w = PluginOrigin::winner(m_pluginOriginCache.value(filename.toLower()));
    if (!w.isEmpty()) emit goToOriginMod(w);
}

} // namespace solero
