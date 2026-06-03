#pragma once
#include <QWidget>
#include "deploy/ConflictIndex.h"

class QTreeWidget;

namespace solero {

class ConflictsTab : public QWidget {
    Q_OBJECT
public:
    explicit ConflictsTab(QWidget* parent = nullptr);
    void setConflictIndex(const ConflictIndex& index);
    void showMod(const QString& modId);

private:
    QTreeWidget*  m_tree;
    ConflictIndex m_conflicts;
    QString       m_currentModId;
    void refresh();
};

} // namespace solero
