#pragma once
#include <QTabWidget>
#include <QHash>
#include <QStringList>
#include "deploy/ConflictIndex.h"
#include "core/Profile.h"

class QLabel;

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
    void showPluginNotice(const QString& text);
    void hidePluginNotice();

    // Invalidate cached per-mod plugin-filename lists used for selection highlight.
    // Empty id clears all; a specific id removes just that mod's entry. Call only
    // when a mod's staged Data files actually change.
    void invalidateModPluginCache(const QString& id = QString());

public slots:
    void onSelectionChanged(const QStringList& ids);
    void showDataFor(const QString& modId);

private:
    PluginListView* m_pluginsTab;
    QLabel*         m_pluginNotice = nullptr;
    DataTab*        m_dataTab;
    ConflictsTab*   m_conflictsTab;
    DownloadsTab*   m_downloadsTab;
    ConflictIndex   m_conflictIndex;
    Profile* m_currentProfile = nullptr;
    // Cache of each mod's staged Data plugin filenames (*.esp/*.esm/*.esl),
    // keyed by mod id. Filled lazily in onSelectionChanged.
    QHash<QString, QStringList> m_modPluginCache;
};

} // namespace solero
