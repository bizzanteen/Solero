#pragma once
#include <QTabWidget>
#include <QHash>
#include <QStringList>
#include "deploy/ConflictIndex.h"
#include "core/Profile.h"

class QLabel;
class QPushButton;

namespace solero {
class PluginListView;
class DataTab;
class ConflictsTab;
class DownloadsTab;
class LootRulesEditor;
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
    void showPluginNotice(const QString& text);
    void hidePluginNotice();
    // Enable/disable the Plugins-tab "Sort Now" (LOOT) button.
    void setSortButtonEnabled(bool enabled);
    // Forwarded from the Plugins view: a manual reorder dirtied the load order.
    PluginListView* pluginsView() const { return m_pluginsTab; }

    // Invalidate cached per-mod plugin-filename lists used for selection highlight.
    // Empty id clears all; a specific id removes just that mod's entry. Call only
    // when a mod's staged Data files actually change.
    void invalidateModPluginCache(const QString& id = QString());

signals:
    // Emitted when the user clicks "Sort Now" to run LOOT.
    void sortRequested();

public slots:
    void onSelectionChanged(const QStringList& ids);
    void showDataFor(const QString& modId);

private:
    PluginListView* m_pluginsTab;
    QLabel*         m_pluginNotice = nullptr;
    QPushButton*    m_sortBtn = nullptr;
    DataTab*        m_dataTab;
    LootRulesEditor* m_lootRulesTab;
    ConflictsTab*   m_conflictsTab;
    DownloadsTab*   m_downloadsTab;
    ConflictIndex   m_conflictIndex;
    Profile* m_currentProfile = nullptr;
    // Cache of each mod's staged Data plugin filenames (*.esp/*.esm/*.esl),
    // keyed by mod id. Filled lazily in onSelectionChanged.
    QHash<QString, QStringList> m_modPluginCache;
};

} // namespace solero
