#pragma once
#include <QTabWidget>
#include <QHash>
#include <QStringList>
#include <QLineEdit>
#include <QTimer>
#include "deploy/ConflictIndex.h"
#include "core/Profile.h"

class QLabel;
class QPushButton;

namespace solero {
class PluginListView;
class DataTab;
class ConflictsTab;
class DownloadsTab;
}

namespace solero {

class RightPane : public QTabWidget {
    Q_OBJECT
public:
    explicit RightPane(QWidget* parent = nullptr);
    // reconcilePlugins=false skips the (expensive) game-data plugin scan+save -
    // used during a deployed-profile switch where a redeploy will reconcile once.
    void setProfile(Profile* profile, bool reconcilePlugins = true);
    void refreshPlugins(Profile* profile);
    void setConflictIndex(const ConflictIndex& index);
    DownloadsTab* downloadsTab() const { return m_downloadsTab; }
    void showDownloadsTab();
    // Switch to the Plugins tab and select + scroll to the named plugin (jump-to
    // from the Problems panel).
    void selectPlugin(const QString& filename);
    // Switch to the Plugins tab and focus + select its search box (Ctrl+Shift+F).
    void focusPluginSearch();
    void showPluginNotice(const QString& text);
    void hidePluginNotice();
    // Enable/disable the Plugins-tab "Sort Now" (LOOT) button. An empty tooltip
    // leaves the current one in place.
    void setSortButtonEnabled(bool enabled, const QString& tooltip = QString());
    // Reflect the profile's "Lock Order" flag on the toggle without re-emitting.
    void setLockOrderChecked(bool checked);
    // Forwarded from the Plugins view: a manual reorder dirtied the load order.
    PluginListView* pluginsView() const { return m_pluginsTab; }

    // Invalidate cached per-mod plugin-filename lists used for selection highlight.
    // Empty id clears all; a specific id removes just that mod's entry. Call only
    // when a mod's staged Data files actually change.
    void invalidateModPluginCache(const QString& id = QString());

signals:
    // Emitted when the user clicks "Sort Now" to run LOOT.
    void sortRequested();
    // Emitted when the user toggles the "Lock Order" button (true = locked).
    void lockOrderToggled(bool checked);
    // Emitted when the user clicks "LOOT Rules" to open the rules editor.
    void lootRulesRequested();
    // Emitted when the user clicks "Backup LO" to snapshot the load order.
    void backupLoRequested();
    // Emitted when the user clicks "Restore LO…" to restore a snapshot.
    void restoreLoRequested();
    // Emitted when a per-file rule (hide / winner override) changed in the Data
    // or Conflicts tab - MainWindow uses it to mark the deployment dirty.
    void fileRulesChanged();
    // Forwarded from the Data tab: rename/delete a staged file or folder.
    // MainWindow performs the filesystem op on the mod's staging dir.
    void renameRequested(const QString& modId, const QString& relPath,
                         const QString& newName, bool isFolder);
    void deleteRequested(const QString& modId, const QString& relPath, bool isFolder);
    // Highlight a clicked plugin's providers in the mod pane: modId -> 1 winner /
    // 2 other provider. Empty map clears the highlight.
    void highlightOriginMods(const QHash<QString,int>& roles);
    // Navigate the mod pane to a plugin's winning origin mod.
    void goToOriginMod(const QString& modId);

public slots:
    void onSelectionChanged(const QStringList& ids);
    void showDataFor(const QString& modId);

private slots:
    void onPluginClicked(const QString& filename);
    void onPluginActivated(const QString& filename);

private:
    void ensurePluginOriginIndex();
    // True if the mod's staging root contains at least one deployable LOOSE file
    // (not a plugin, not per-mod metadata) - i.e. the Data tab would show real
    // content for it. Used to decide whether to auto-switch a single-mod
    // selection to the Plugins tab.
    bool modHasLooseData(const QString& modId) const;
    // The plugin filename from `filenames` that sorts earliest in the current
    // load order (profile's PluginList), or empty if none are present.
    QString firstPluginInLoadOrder(const QStringList& filenames) const;

    PluginListView* m_pluginsTab;
    QLabel*         m_pluginNotice = nullptr;
    QPushButton*    m_sortBtn = nullptr;
    QPushButton*    m_lockBtn = nullptr;
    QLineEdit*      m_pluginSearch = nullptr;
    QTimer*         m_pluginSearchDebounce = nullptr;
    DataTab*        m_dataTab;
    ConflictsTab*   m_conflictsTab;
    DownloadsTab*   m_downloadsTab;
    ConflictIndex   m_conflictIndex;
    Profile* m_currentProfile = nullptr;
    // Cache of each mod's staged Data plugin filenames (*.esp/*.esm/*.esl),
    // keyed by mod id. Filled lazily in onSelectionChanged.
    QHash<QString, QStringList> m_modPluginCache;
    // Reverse index: lowercased plugin filename -> providing mod ids (low->high
    // priority; last = winner). Built lazily, invalidated with m_modPluginCache.
    QHash<QString, QStringList> m_pluginOriginCache;
    bool m_originIndexBuilt = false;
};

} // namespace solero
