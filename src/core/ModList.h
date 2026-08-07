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
    // Lift the entries at srcRaws (a set: sorted, deduped, out-of-range ignored)
    // and reinsert them as one block in their original relative order, just before
    // whatever currently sits at dstRaw. Anchored by identity so it survives the
    // lift; dstRaw >= count appends. True if the order changed.
    bool reorder(QList<int> srcRaws, int dstRaw);
    void setEnabled(const QString& id, bool enabled);
    void update(const QString& id, const ModEntry& updated);

    // Order snapshots for undo/redo. setOrder ignores unknown ids, and entries
    // missing from `ids` keep their relative position at the end. True if the
    // order changed.
    QStringList orderIds() const;
    bool setOrder(const QStringList& ids);

    // Multi-file grouping. The invariant: a parent is a Mod immediately followed by
    // a contiguous run of child Mods whose parentId is the parent's id.
    //
    // groupUnder moves the child to the end of that run. Returns true only if it
    // actually moved - a re-parent that leaves the order alone returns false.
    bool groupUnder(const QString& childId, const QString& parentId);
    // ungroup: clear child.parentId and move it to just after its former parent's
    // group block, so it becomes a top-level mod directly below the group.
    // Returns true only if the entry was actually repositioned.
    bool ungroup(const QString& childId);
    // Count of the contiguous run of child Mods stored after the mod at rawIndex
    // (children = Mod entries whose parentId == that mod's id).
    int childRunCount(int parentRaw) const;

    // Restore the group invariant: orphaned children go top-level, a child of a
    // child re-points to the grandparent (one level only), then a stable reorder
    // puts each run back after its parent. Idempotent; call on load and after
    // anything that may displace a child.
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
