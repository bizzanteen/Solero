#pragma once
#include <QWidget>
#include "deploy/ConflictIndex.h"
#include "core/Profile.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;

namespace solero {

class ConflictsTab : public QWidget {
    Q_OBJECT
public:
    explicit ConflictsTab(QWidget* parent = nullptr);
    void setProfile(Profile* profile) { m_profile = profile; }
    void setConflictIndex(const ConflictIndex& index);
    void showMod(const QString& modId);

signals:
    // Double-clicking a conflict file row asks the right pane to show that mod's
    // Data view. relPath is the conflicting file's path (relative to Data).
    void fileActivated(const QString& modId, const QString& relPath);

private:
    QTreeWidget*  m_tree;
    QLineEdit*    m_filter = nullptr;
    Profile*      m_profile = nullptr;
    ConflictIndex m_conflicts;
    QString       m_currentModId;
    QString       m_filterText;
    void refresh();
    void applyFilter();
    // Resolve a mod id to its display name via the active profile.
    QString modDisplayName(const QString& modId) const;
};

} // namespace solero
