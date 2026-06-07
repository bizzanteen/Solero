#pragma once
#include <QTreeView>
#include <QHash>
#include <QPair>
#include "core/Profile.h"
#include "deploy/ConflictIndex.h"

namespace solero {
class ModListModel;

class ModListView : public QTreeView {
    Q_OBJECT
public:
    // Quick-filter predicates by mod state (alongside the name filter).
    enum class StateFilter { All, Conflicts, UpdateAvailable, Enabled, Disabled, MissingDep };

    explicit ModListView(QWidget* parent = nullptr);
    void setProfile(Profile* profile);
    // Select + scroll to the Mod row with the given id (jump-to from the Problems
    // panel). No-op if the id isn't present or its row is hidden (collapsed group).
    void selectModById(const QString& id);
    void deleteSelectedMods();
    // Hide Mod rows whose name doesn't contain `text` (case-insensitive).
    // Separators with no visible children are hidden while filtering; the
    // Overwrite row stays visible. Empty text shows all.
    void setFilter(const QString& text);
    // Restrict the list to mods matching a state predicate (combined with the
    // name filter). StateFilter::All clears the state restriction.
    void setStateFilter(StateFilter state);
    // Set the enabled state of every selected Mod row at once (save + one
    // modsChanged signal). Separators / Overwrite are ignored.
    void setSelectedModsEnabled(bool enabled);
    // Pass-through to the underlying model's cache invalidation. Call only when a
    // mod's staged files change. Empty id clears the whole empty/Overwrite cache.
    void invalidateModCache(const QString& id = QString());
    // Pass-through to the underlying model's update-available indicator.
    // Key = mod id; value = {installedVersion, latestVersion} (mods with updates).
    void setUpdateInfo(const QHash<QString, QPair<QString,QString>>& info);
    // Provide the per-file conflict index so selecting a mod highlights its
    // conflicting mods (green = overwrites it, red = overwritten by it), MO2-style.
    void setConflictIndex(const ConflictIndex& index);
    // Repaint the Flags column (e.g. after a mod note was edited).
    void refreshFlags();

signals:
    // Emitted on selection change. Each entry is a mod id, "__overwrite__" for the
    // Overwrite row, or "__separator__" for separator rows. Empty list = nothing selected.
    void modsSelected(const QStringList& ids);
    void reinstallRequested(const QString& modId);
    void redownloadRequested(const QString& modId);
    void endorseRequested(const QString& modId);
    void identifyRequested(const QString& modId);
    void updateRequested(const QString& modId);
    void modsChanged();
    void modActivated(const QString& modId);
    // Right-click the Overwrite row -> "Create Mod from Overwrite…": promote the
    // captured overwrite files into a new, named, sortable mod (MO2 parity).
    void createModFromOverwriteRequested();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    ModListModel* m_model;
    QString m_filter;
    StateFilter m_stateFilter = StateFilter::All;
    bool m_didAutoSize = false;
    // True when the given mod entry passes the active state predicate.
    bool matchesState(const ModEntry* entry) const;
    // Right-click header menu: toggle which columns are shown (Name is mandatory);
    // persists the hidden set to AppConfig.
    void showHeaderMenu(const QPoint& pos);
    void applyHiddenColumns(); // apply AppConfig's persisted hidden-column set
    ConflictIndex m_conflicts;
    // Recompute the green/red conflict highlight for the current single selection.
    void updateConflictHighlights();
    void autoSizeColumns();
    void fillNameColumn();
    void applyFilter();
    // Span separator rows across the full width (content in column 0) and reset
    // spans on non-separator rows. Must be re-run after every model reset.
    void applyRowSpans();
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
