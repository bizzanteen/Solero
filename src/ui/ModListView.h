#pragma once
#include <QTreeView>
#include <QHash>
#include <QPair>
#include "core/Profile.h"
#include "deploy/ConflictIndex.h"

class QTimer;
class QDragMoveEvent;
class QDragLeaveEvent;

namespace solero {
class ModListModel;

class ModListView : public QTreeView {
    Q_OBJECT
public:
    // Quick-filter predicates by mod state (alongside the name filter).
    enum class StateFilter { All, Conflicts, UpdateAvailable, Enabled, Disabled, MissingDep };
    // Quick-filter by a per-mod flag, combined with name/state/category.
    enum class FlagFilter { All, Fomod, Output, HasNote };

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
    // facets, combined with the name + state filters. Category restricts to
    // mods under the separator with the given name (empty = any category); Flag
    // restricts by a per-mod flag (FlagFilter::All clears it).
    void setCategoryFilter(const QString& separatorName);
    void setFlagFilter(FlagFilter flag);
    // Ordered, de-duplicated separator (category) names in the active profile, for
    // populating the category-filter control. Empty when there's no profile.
    QStringList sectionNames() const;
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
    // Forward a plugin-origin highlight (from the Plugins tab) to the model.
    void setPluginOriginHighlights(const QHash<QString,int>& roles);
    // Repaint the Flags column (e.g. after a mod note was edited).
    void refreshFlags();

public slots:
    // Undo / redo the most recent mod-list reorder for the active profile. Wired
    // to the LeftPane's Undo/Redo toolbar buttons. After the order is restored,
    // selection is preserved where still valid.
    void undoMove();
    void redoMove();

signals:
    // Emitted on selection change. Each entry is a mod id, "__overwrite__" for the
    // Overwrite row, or "__separator__" for separator rows. Empty list = nothing selected.
    void modsSelected(const QStringList& ids);
    void reinstallRequested(const QString& modId);
    void redownloadRequested(const QString& modId);
    void endorseRequested(const QString& modId);
    // track / untrack the mod in the user's Nexus tracking centre.
    void trackRequested(const QString& modId);
    void untrackRequested(const QString& modId);
    void viewNexusPageRequested(const QString& modId);
    void updateRequested(const QString& modId);
    void modsChanged();
    // Re-emitted from the model for order-only changes; wired to a lighter handler
    // that skips the plugin rescan + dependency-health walk (see MainWindow).
    void modsReordered();
    void modActivated(const QString& modId);
    // Right-click the Overwrite row -> "Create Mod from Overwrite…": promote the
    // captured overwrite files into a new, named, sortable mod (MO2 parity).
    void createModFromOverwriteRequested();
    // Right-click the Community Shaders base mod -> "Clear Shader Cache": wipe the
    // compiled shader cache (CS recompiles it on next launch).
    void clearShaderCacheRequested(const QString& modId);
    // Re-emitted from the model whenever the reorder undo/redo stack state changes,
    // so the LeftPane can enable/disable its Undo/Redo buttons.
    void undoRedoStateChanged(bool canUndo, bool canRedo);
    // Re-emitted from the model when a mod's active version variant is switched via
    // the Version-column dropdown; MainWindow rescans plugins and auto-redeploys.
    void variantSwitched(const QString& modId);
    // A profile save (after a list edit) failed to write to disk; MainWindow shows
    // a status-bar warning so the change loss isn't silent.
    void saveFailed();
    // Emitted by selectModById when it must drop an active name/state filter to
    // reveal a jump-to target. The filter widgets live in MainWindow's left pane,
    // so MainWindow clears its search box + resets the state combo in response;
    // those widgets' own signals then drive setFilter("")/setStateFilter(All),
    // keeping the visible filter box and the list in sync.
    void filterCleared();

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    // Capture the dragged mods' ids before the drop, then re-select them at their
    // new rows afterwards so a multi-mod drag keeps its selection (the base impl
    // clears it on the model reset). Current index lands on the first moved mod.
    void dropEvent(QDropEvent* event) override;
    // Hover-to-expand: dragging onto a collapsed separator for ~650 ms auto-expands
    // it so the user can drop inside (file-manager-style spring-loaded folder).
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;

private:
    ModListModel* m_model;
    // Spring-loaded hover-expand of a collapsed separator during a drag.
    QTimer* m_autoExpandTimer = nullptr;
    int m_autoExpandRow = -1; // visible row currently armed for auto-expand, or -1
    QString m_filter;
    StateFilter m_stateFilter = StateFilter::All;
    QString m_categoryFilter;                  // empty = any category
    FlagFilter m_flagFilter = FlagFilter::All;
    bool m_didAutoSize = false;
    // True when the given mod entry passes the active state predicate.
    bool matchesState(const ModEntry* entry) const;
    // Right-click header menu: toggle which columns are shown (Name is mandatory);
    // persists the hidden set to AppConfig.
    void showHeaderMenu(const QPoint& pos);
    void applyHiddenColumns(); // apply AppConfig's persisted hidden-column set
    // Persist the current header layout (column widths) to AppConfig; debounced via
    // m_headerSaveTimer so a resize drag writes config once, not per pixel.
    void saveHeaderState();
    QTimer* m_headerSaveTimer = nullptr;
    // Save the active profile after a list edit; emits saveFailed() on write error.
    void saveProfile();
    ConflictIndex m_conflicts;
    // Recompute the green/red conflict highlight for the current single selection.
    void updateConflictHighlights();
    void autoSizeColumns();
    void applyFilter();
    // Span separator rows across the full width (content in column 0) and reset
    // spans on non-separator rows. Must be re-run after every model reset.
    void applyRowSpans();
    // Selected mod-row ids in list (raw) order; separators/Overwrite excluded.
    QStringList selectedModIds() const;
    // Re-select the Mod rows with the given ids (visible ones only) and set the
    // current index to the first still-visible id. Clears any prior selection.
    void selectModsByIds(const QStringList& ids);
    // Group the selected mods: first (topmost) becomes parent, rest nested under.
    void groupSelectedMods();
    // Ungroup a child mod: clear its parentId and move it below the group block.
    void ungroupMod(const QString& id);
    void onAddSeparator();
    void onAddSeparatorAt(int visibleRow);
    void onEditSeparator(int visibleRow);
    void onDeleteSeparator(int visibleRow);
    // Nest a separator one level deeper (sub-category), clamped so its level never
    // exceeds the nearest preceding separator's level + 1 (no orphan depth jumps).
    void onIndentSeparator(int visibleRow);
    // Promote a separator one level shallower (min 0).
    void onOutdentSeparator(int visibleRow);
    void showIconPicker(int visibleRow, const QPoint& globalPos);
};
}
