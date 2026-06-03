#pragma once
#include <QWidget>
#include "deploy/ConflictIndex.h"
#include "core/Profile.h"

class QTreeWidget;

namespace solero {

class DataTab : public QWidget {
    Q_OBJECT
public:
    explicit DataTab(QWidget* parent = nullptr);
    void setProfile(Profile* profile);
    void setConflictIndex(const ConflictIndex& index);
    void showMod(const QString& modId);

private:
    QTreeWidget*  m_tree;
    Profile*      m_profile = nullptr;
    ConflictIndex m_conflicts;
    QString       m_currentModId;
    void refresh();
};

} // namespace solero
