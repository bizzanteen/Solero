#pragma once
#include <QTreeView>
#include <QHash>
#include <QPair>
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
    // Pass-through to the underlying model's update-available indicator.
    // Key = mod id; value = {installedVersion, latestVersion} (mods with updates).
    void setUpdateInfo(const QHash<QString, QPair<QString,QString>>& info);

signals:
    // Emitted on selection change. Each entry is a mod id, "__overwrite__" for the
    // Overwrite row, or "__separator__" for separator rows. Empty list = nothing selected.
    void modsSelected(const QStringList& ids);
    void reinstallRequested(const QString& modId);
    void endorseRequested(const QString& modId);
    void identifyRequested(const QString& modId);
    void updateRequested(const QString& modId);
    void modsChanged();
    void modActivated(const QString& modId);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    ModListModel* m_model;
    QString m_filter;
    bool m_didAutoSize = false;
    void autoSizeColumns();
    void applyFilter();
    // Selected mod-row ids in list (raw) order; separators/Overwrite excluded.
    QStringList selectedModIds() const;
    // Group the selected mods: first (topmost) becomes parent, rest nested under.
    void groupSelectedMods();
    // Ungroup a child mod: clear its parentId and move it below the group block.
    void ungroupMod(const QString& id);
    void onAddSeparator();
    void onAddSeparatorAt(int visibleRow);
    void onEditSeparator(int visibleRow);
    void onDeleteSeparator(int visibleRow);
    void showIconPicker(int visibleRow, const QPoint& globalPos);
};
}
