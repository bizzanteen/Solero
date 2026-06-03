#pragma once
#include <QWidget>
#include <QSet>
#include "deploy/ConflictIndex.h"
#include "core/Profile.h"

class QTreeWidget;
class QTreeWidgetItem;

namespace solero {

class DataTab : public QWidget {
    Q_OBJECT
public:
    explicit DataTab(QWidget* parent = nullptr);
    void setProfile(Profile* profile);
    void setConflictIndex(const ConflictIndex& index);
    void showMod(const QString& modId);

private slots:
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onFileSaved(const QString& filePath);

private:
    QTreeWidget*  m_tree;
    Profile*      m_profile = nullptr;
    ConflictIndex m_conflicts;
    QString       m_currentModId;
    QString       m_currentStagingRoot;
    QSet<QString> m_editedRelPaths;

    void refresh();
    QString stagingRootFor(const QString& modId) const;
    static QString editedMarkerPath(const QString& stagingRoot);
    void loadEdited(const QString& stagingRoot);
    void saveEdited(const QString& stagingRoot) const;
};

} // namespace solero
