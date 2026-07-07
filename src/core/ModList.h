#pragma once
#include "Types.h"
#include <QJsonDocument>
#include <QHash>

namespace solero {

class ModList {
public:
    void append(const ModEntry& entry);
    void remove(const QString& id);
    void move(int from, int to);
    // Move a contiguous block of `count` entries starting at raw index `from` so
    // that the block's first element lands at raw index `to` (interpreted against
    // the list with the block removed). Used to drag a separator together with
    // every mod in its section.
    void moveSection(int from, int count, int to);
    // Lift the entries at the given raw indices (treated as a SET: sorted ascending
    // and deduped; out-of-range indices ignored) and reinsert them as one contiguous
    // block, in their original relative (ascending-index) order, at the position
    // indicated by `dstRaw`. `dstRaw` is interpreted against the original list as
    // "insert the block just before whatever entry currently sits at dstRaw", using
    // anchor-by-identity so it survives the lift. dstRaw >= count appends. Returns
    // true iff the list order actually changed. Used to drag a non-contiguous
    // multi-selection so the selected mods drop together as a contiguous block.
    bool reorder(QList<int> srcRaws, int dstRaw);
    void setEnabled(const QString& id, bool enabled);
    void update(const QString& id, const ModEntry& updated);

    // Order snapshot helpers (used by the undo/redo stacks). orderIds() returns
    // every entry id in raw order; setOrder() rearranges the backing list so its
    // entries appear in the given id order. Ids absent from the list are ignored;
    // any entry whose id is not in `ids` keeps its relative position appended at
    // the end (defensive - snapshots always cover the whole list). Returns true
    // iff the order actually changed.
    QStringList orderIds() const;
    bool setOrder(const QStringList& ids);

    // Multi-file grouping (Stage M2). The storage invariant is: a group PARENT is
    // a Mod immediately followed by a CONTIGUOUS run of child Mods whose parentId
    // == the parent's id.
    //
    // groupUnder: set child.parentId = parentId and reposition the child so it
    // sits at the END of the parent's existing contiguous child run (i.e. right
    // after the parent + any current children). No-op if either id is missing,
    // they're the same, or parentId refers to a non-Mod.
    // Returns true only if the entry was actually REPOSITIONED (the deploy-relevant
    // change); a pure re-parent that leaves order unchanged returns false.
    bool groupUnder(const QString& childId, const QString& parentId);
    // ungroup: clear child.parentId and move it to just after its former parent's
    // group block, so it becomes a top-level mod directly below the group.
    // Returns true only if the entry was actually repositioned.
    bool ungroup(const QString& childId);
    // Count of the contiguous run of child Mods stored after the mod at rawIndex
    // (children = Mod entries whose parentId == that mod's id).
    int childRunCount(int parentRaw) const;

    // Enforce the group invariant defensively. For every Mod with a non-empty
    // parentId: if the parent is missing or not a Mod, clear parentId (orphan ->
    // top-level); if the parent is itself a child, re-point to the grandparent
    // (flatten to a single level). Then stable-reorder so each child sits in a
    // contiguous run immediately after its parent. Top-level order and per-group
    // child order are preserved. Idempotent. Call after any operation that may
    // have displaced a child, and on load.
    void normalizeGroups();

    // Version variants (Keep Both). See Types.h ModVariant for the mirror invariant.
    bool keepBothAddVariant(const QString& id, const ModVariant& v);
    bool setActiveVariant(const QString& id, int index);
    bool replaceActiveVersion(const QString& id, const ModVariant& v,
                              QString* retiredFolder = nullptr);
    // Index of the variant that owns the given Nexus fileId, or -1 if the mod has
    // no variants / no match / empty fileId. Used so a same-file reinstall targets
    // the variant that actually owns the incoming file, not just the active one.
    int variantIndexByFileId(const QString& id, const QString& fileId) const;
    // Overwrite variants[index] in place (bounds-checked). Re-syncs the entry's
    // mirror fields iff index == activeVariant. Returns false on unknown id or
    // out-of-range index.
    bool updateVariant(const QString& id, int index, const ModVariant& v);
    void normalizeVariants();

    int count() const { return m_entries.size(); }
    const ModEntry& at(int index) const { return m_entries.at(index); }
    // Direct access to the backing list (e.g. for staging-folder migration).
    QList<ModEntry>& entries() { return m_entries; }
    const QList<ModEntry>& entries() const { return m_entries; }
    ModEntry* findById(const QString& id);
    const ModEntry* findById(const QString& id) const;
    // Find a Mod entry by its Nexus mod id (skip the entry whose id == skipId, so
    // callers can ignore the mod they're currently installing/reinstalling).
    // Returns the first match, or nullptr. Empty nexusModId never matches.
    ModEntry* findByNexusId(const QString& nexusModId, const QString& skipId = {});
    // Find a Mod entry by Nexus mod id and file id (both must be non-empty and
    // match). Returns the first match, or nullptr. This is the reliable mod
    // identity: same (modId,fileId) == literally the same downloaded file.
    ModEntry* findByNexusFile(const QString& nexusModId,
                              const QString& nexusFileId,
                              const QString& skipId = {});
    // Find a Mod entry by display name (case-insensitive; skip the given id).
    ModEntry* findByName(const QString& name, const QString& skipId = {});

    // Find the Community Shaders BASE mod: the first Mod with nexusModId=="86492"
    // or name "Community Shaders" (case-insensitive). Returns nullptr if absent.
    // (The managed shader cache is no longer a mod-list entry - see
    // Profile::shaderCache - so there's nothing to exclude here.)
    const ModEntry* findCommunityShaders() const;

    QJsonDocument toJson() const;
    static ModList fromJson(const QJsonDocument& doc);

    bool saveToFile(const QString& path) const;
    static ModList loadFromFile(const QString& path);

    using const_iterator = QList<ModEntry>::const_iterator;
    const_iterator begin() const { return m_entries.begin(); }
    const_iterator end()   const { return m_entries.end(); }

private:
    QList<ModEntry> m_entries;
    // id -> raw index into m_entries, backing findById. Rebuilt after any
    // structural mutation that reorders/removes entries; patched incrementally on
    // append. Keeps first-match semantics for (defensive) duplicate ids.
    QHash<QString, int> m_idIndex;
    void rebuildIndex();
    void propagateEnabled(const QString& parentId, bool enabled);
};

} // namespace solero
