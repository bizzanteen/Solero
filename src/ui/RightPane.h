#pragma once
#include <QTabWidget>
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
    void setProfile(Profile* profile);
    void refreshPlugins(Profile* profile);
    void setConflictIndex(const ConflictIndex& index);
    DownloadsTab* downloadsTab() const { return m_downloadsTab; }
    void showPluginNotice(const QString& text);
    void hidePluginNotice();

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
};

} // namespace solero
