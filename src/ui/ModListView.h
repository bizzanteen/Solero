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
    // Hide Mod rows whose name doesn't contain `text` (case-insensitive).
    // Separators and the Overwrite row stay visible. Empty text shows all.
    void setFilter(const QString& text);
    // Set the enabled state of every selected Mod row at once (save + one
    // modsChanged signal). Separators / Overwrite are ignored.
    void setSelectedModsEnabled(bool enabled);
    // Pass-through to the underlying model's cache invalidation. Call only when a
    // mod's staged files change. Empty id clears the whole empty/Overwrite cache.
    void invalidateModCache(const QString& id = QString());

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
    void keyPressEvent(QKeyEvent* event) override;

private:
    ModListModel* m_model;
    QString m_filter;
    void applyFilter();
    void onAddSeparator();
    void onAddSeparatorAt(int visibleRow);
    void onEditSeparator(int visibleRow);
    void onDeleteSeparator(int visibleRow);
    void showIconPicker(int visibleRow, const QPoint& globalPos);
};
}
