#pragma once
#include <QAbstractItemModel>
#include <QHash>
#include <QPair>
#include <QStringList>
#include <QSet>
#include "core/Profile.h"
#include "deploy/ConflictIndex.h"

namespace solero {

class ModListModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column { ColEnabled = 0, ColPriority, ColName, ColVersion, ColFlags, ColCount };

    // For the synthetic [Overwrite] row only: the active profile's name, so the
    // ColName delegate can render "[Overwrite] - <ProfileName>" with the name styled.
    // Invalid QVariant for every other row.
    enum Role {
        OverwriteProfileRole = Qt::UserRole + 1,
        // Version-variant (Keep Both) roles, queried by the ColVersion delegate:
        VariantListRole  = Qt::UserRole + 41, // QStringList of variant versions (empty if <2)
        VariantIndexRole = Qt::UserRole + 42, // int: active variant index
    };

    explicit ModListModel(QObject* parent = nullptr);
    void setProfile(Profile* profile);
    Profile* profile() const { return m_profile; }

    QModelIndex index(int row, int col, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex&) const override { return {}; }
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    Qt::DropActions supportedDropActions() const override;
    bool moveRows(const QModelIndex&, int src, int count, const QModelIndex&, int dst) override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData*, Qt::DropAction, int row, int col, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData*, Qt::DropAction, int row, int col, const QModelIndex& parent) override;

    // Map from visible row index to raw ModList index (-1 = Overwrite)
    int rawIndexForRow(int visibleRow) const;
    int rawToVisible(int rawIndex) const;
    // Current visible row of the Mod with the given id, or -1 if it isn't present
    // or its row is hidden (collapsed group/separator). Used to re-select moved
    // mods after a drag-drop reorder.
    int rowForModId(const QString& id) const;
    const ModEntry* entryAt(int visibleRow) const;
    void toggleCollapse(int visibleRow);
    // Expand a collapsed separator during an in-flight drag without a model reset
    // (a reset tears down the view's drag state and the drop is lost). Reveals the
    // section's rows via beginInsertRows; no-op unless `visibleRow` is a collapsed
    // separator. Falls back to rebuild() if the revealed rows aren't contiguous.
    void expandSeparatorDuringDrag(int visibleRow);
    // Toggle the collapsed state of a group-PARENT mod (mirrors toggleCollapse,
    // which is for separators). No-op if the entry isn't a group parent.
    void toggleModCollapse(int visibleRow);

    // Multi-file group helpers (operate on raw ModList indices).
    // A parent is a Mod immediately followed by ≥1 Mod whose parentId == its id.
    bool isGroupParent(int raw) const;
    // A child is a Mod with a non-empty parentId.
    bool isGroupChild(int raw) const;
    // Count of the contiguous run of child Mods after a parent.
    int groupChildCount(int parentRaw) const;
    void rebuild();  // call after any structural change

    // Move the given mods to the BOTTOM of separator `sepId`'s section (just before
    // the next separator at the same/shallower level, or the end of the list). Group
    // parents carry their children. Persists + records undo + emits modsChanged, like
    // a manual reorder. Returns true iff the order actually changed.
    bool moveModsToSeparatorEnd(const QStringList& modIds, const QString& sepId);

    // While searching, reveal mods inside collapsed separators / collapsed group
    // parents so a name/state filter can match them. Toggling this does a model
    // reset and rebuilds the visible rows; it never mutates persisted collapse
    // state (normal collapse behaviour resumes when turned off).
    void setSearchExpandAll(bool on);
    void setDependencyWarnings(const QHash<QString,QStringList>& w);
    // Mark mods that have a newer version available. Key = mod id; value =
    // {installedVersion, latestVersion}. Only includes mods with an update.
    void setUpdateInfo(const QHash<QString, QPair<QString,QString>>& info);
    // MO2-style conflict highlight for the selected mod: id -> 1 (green: this mod
    // overwrites the selection) / 2 (red: overwritten by the selection). Empty = clear.
    void setConflictHighlights(const QHash<QString,int>& roles);
    // Plugin-origin highlight (when a plugin is clicked in the Plugins tab): id ->
    // 1 (winner, emphasized) / 2 (other provider). Takes precedence over the
    // selection conflict highlight; the two are mutually exclusive in practice.
    void setPluginOriginHighlights(const QHash<QString,int>& roles);
    // Full per-file conflict index, used to paint always-ON winner/loser flag icons
    // in the Flags column (independent of the transient on-select highlight above).
    void setConflictIndex(const ConflictIndex& index);
    // Re-paint flag indicators (e.g. after a mod note changed). Cheap dataChanged.
    void refreshFlags();

    // State-filter data sources (used by ModListView::applyFilter).
    bool modHasConflict(const QString& id) const {
        return m_overwritingMods.contains(id) || m_overwrittenMods.contains(id);
    }
    bool modHasUpdate(const QString& id) const     { return m_updates.contains(id); }
    bool modHasMissingDep(const QString& id) const { return m_depWarnings.contains(id); }

    // Invalidate cached disk scans (empty-mod / Overwrite "has files"). Call only
    // when a mod's staged files actually change. Empty id clears the whole cache
    // (and the Overwrite cache); a specific id removes just that entry.
    void invalidateModCache(const QString& id = QString());

    // --- Mod-list reorder undo/redo (active profile only) ---------------------
    // Snapshots are the ordered list of every entry id (raw order); restoring one
    // routes through the same reset+persist+modsChanged path a manual reorder
    // uses, so persistence and side-effects fire exactly as usual.
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }
    // Restore the previous / next reorder snapshot. No-op if the matching stack
    // is empty. Returns true iff the order changed.
    bool undoOrder();
    bool redoOrder();
    // Drop both stacks (e.g. on profile change so undo can't cross profiles).
    void clearUndoRedo();

signals:
    void modsChanged();
    // Emitted after setData switches a mod's active version variant (VariantIndexRole);
    // MainWindow rescans the mod's plugins and auto-redeploys.
    void variantSwitched(const QString& modId);
    // Emitted whenever the undo/redo stack state changes (push, undo, redo, clear).
    void undoRedoStateChanged(bool canUndo, bool canRedo);

private:
    Profile* m_profile = nullptr;
    QList<int> m_visibleRows; // raw indices into ModList, -1 = Overwrite
    // Precomputed Priority column: raw index of a Mod -> its 1-based contiguous
    // position among Mod-type entries (children included), in raw order. Rebuilt
    // in rebuildVisibleRows() so the Priority cell is O(1) instead of O(n).
    QHash<int,int> m_priorityByRaw;
    QHash<QString,QStringList> m_depWarnings;
    QHash<QString, QPair<QString,QString>> m_updates; // modId -> {installed, latest}
    QHash<QString,int> m_conflictHi; // modId -> 1 green / 2 red (selection conflicts)
    QHash<QString,int> m_pluginOriginHi; // modId -> 1 winner / 2 other provider
    ConflictIndex m_conflicts;       // full index for the always-on Flags icons
    QSet<QString> m_overwritingMods; // mods that win ≥1 file conflict (overwrite others)
    QSet<QString> m_overwrittenMods; // mods that lose ≥1 file conflict (overwritten)
    mutable QHash<QString,bool> m_emptyCache;
    mutable int m_overwriteHasFiles = -1; // tri-state cache: -1 unknown, 0 no, 1 yes
    bool m_searchExpandAll = false; // ignore collapse while filtering (state untouched)

    // Reorder undo/redo: each snapshot is the full ordered id list of the active
    // profile's mod list. Capped at kUndoCap each.
    static constexpr int kUndoCap = 100;
    QList<QStringList> m_undoStack;
    QList<QStringList> m_redoStack;
    // Push the current order onto the undo stack and clear redo. Call before a
    // user-initiated reorder mutation. No-op without a profile.
    void pushUndoSnapshot();
    // Apply a target id order through the shared reset+persist path so the profile
    // saves and modsChanged fires. Returns true iff the order changed.
    bool applyOrder(const QStringList& ids);

    void rebuildVisibleRows();
    bool isModEmpty(const QString& id) const;
    // Drag a non-contiguous multi-selection: expand each visible source row into
    // its unit (mod / group block / separator section), lift them all and drop
    // them as one contiguous block at dstVisible. Returns true iff anything moved.
    bool moveSelection(const QList<int>& srcVisibleRows, int dstVisible);
};

} // namespace solero
