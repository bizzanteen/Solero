#pragma once
#include <QTreeView>
#include "core/Profile.h"

namespace solero {
class ModListModel;

class ModListView : public QTreeView {
    Q_OBJECT
public:
    explicit ModListView(QWidget* parent = nullptr);
    void setProfile(Profile* profile);

signals:
    void modSelected(const QString& modId); // "__overwrite__" for Overwrite row, empty for none

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    ModListModel* m_model;
    void onAddSeparator();
    void onAddSeparatorAt(int visibleRow);
    void onEditSeparator(int visibleRow);
};
}
