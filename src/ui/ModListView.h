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
    void deleteSelectedMods();

signals:
    // Emitted on selection change. Each entry is a mod id, "__overwrite__" for the
    // Overwrite row, or "__separator__" for separator rows. Empty list = nothing selected.
    void modsSelected(const QStringList& ids);
    void reinstallRequested(const QString& modId);
    void modsChanged();
    void modActivated(const QString& modId);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    ModListModel* m_model;
    void onAddSeparator();
    void onAddSeparatorAt(int visibleRow);
    void onEditSeparator(int visibleRow);
    void onDeleteSeparator(int visibleRow);
    void showIconPicker(int visibleRow, const QPoint& globalPos);
};
}
