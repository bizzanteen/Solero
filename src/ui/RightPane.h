#pragma once
#include <QTabWidget>
#include "deploy/ConflictIndex.h"
#include "core/Profile.h"

namespace solero {
class PluginListView;
class DataTab;
class ConflictsTab;
}

namespace solero {

class RightPane : public QTabWidget {
    Q_OBJECT
public:
    explicit RightPane(QWidget* parent = nullptr);
    void setProfile(Profile* profile);
    void refreshPlugins(Profile* profile);
    void setConflictIndex(const ConflictIndex& index);

public slots:
    void onSelectionChanged(const QStringList& ids);
    void showDataFor(const QString& modId);

private:
    PluginListView* m_pluginsTab;
    DataTab*        m_dataTab;
    ConflictsTab*   m_conflictsTab;
    ConflictIndex   m_conflictIndex;
    Profile* m_currentProfile = nullptr;
};

} // namespace solero
