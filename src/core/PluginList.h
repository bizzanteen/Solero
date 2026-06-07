#pragma once
#include "Types.h"
#include <QString>
#include <QPair>
#include <QHash>

namespace solero {

// Load-order band of a plugin: masters (0) sort before light masters (1),
// which sort before regular plugins (2). Light masters take precedence over
// the plain master flag so ESL-flagged files are grouped with other lights.
int pluginBand(const PluginEntry& p);

class PluginList {
public:
    void append(const PluginEntry& entry);
    void remove(const QString& filename);
    void move(int from, int to);
    void setEnabled(const QString& filename, bool enabled);

    int count() const { return m_plugins.size(); }
    const PluginEntry& at(int index) const { return m_plugins.at(index); }
    PluginEntry* findByFilename(const QString& filename);

    // Valid [lo, hi] destination index range (inclusive) the plugin at `src`
    // may be moved to without violating load-order rules. Indices are expressed
    // in post-removal coordinates (i.e. directly comparable to QList::move's
    // `to` argument: the resulting index of the dragged plugin). Rules:
    //   1. Locked/official block stays on top  -> lo >= number of officials.
    //   2. Band order (master<light<esp)       -> stay within its contiguous band.
    //   3. Master/dependency order             -> load after its masters and
    //      before any plugin that lists it as a master.
    // Filenames are compared case-insensitively. Returns an empty/invalid range
    // (lo > hi) only if `src` is out of bounds.
    QPair<int,int> allowedDropRange(int src) const;

    // True if moving the plugin at `src` to resulting index `to` (post-removal
    // coordinates, as passed to move()) keeps the load order valid per the rules
    // documented on allowedDropRange().
    bool isValidMove(int src, int to) const;

    // Reconcile this list against a saved load-order snapshot - a sequence of
    // {filename, enabled} pairs in the snapshot's load order. MO2 semantics:
    //   • plugins present in both take the snapshot's enabled state and its
    //     relative order;
    //   • plugins in the snapshot but no longer present are ignored;
    //   • plugins present now but absent from the snapshot keep their current
    //     enabled state and drop to the bottom (after the restored ones), in
    //     their current relative order.
    // Band ordering is always preserved: official/locked plugins stay in their
    // forced top band (current relative order), then master < light < esp, so a
    // restore can never produce an invalid load order.
    void restoreSnapshot(const QList<QPair<QString, bool>>& snapshot);

    // Manual load-order control (lock + per-plugin pins)
    // Lock: when set, callers SKIP LOOT auto-sort and keep the manual order -
    // both "Sort Now" and the automatic sort inside deploy become no-ops.
    bool loadOrderLocked() const { return m_loadOrderLocked; }
    void setLoadOrderLocked(bool locked) { m_loadOrderLocked = locked; }

    // Pin: keep a plugin at a chosen load-order index. setPinned(name, true)
    // records the plugin's current index; applyPins() then restores every pinned
    // plugin to its recorded index after a non-manual reorder (LOOT sort /
    // reconcile), clamped to its legal allowedDropRange() slot so the official
    // block and master<light<esp bands are never broken. Filenames are matched
    // case-insensitively. A manual drag of a pinned plugin should re-call
    // setPinned(name, true) to update the recorded index to the new slot.
    void setPinned(const QString& filename, bool pinned);
    bool isPinned(const QString& filename) const;
    int  pinnedIndex(const QString& filename) const;          // -1 if not pinned
    const QHash<QString, int>& pinnedIndices() const { return m_pinned; }
    void setPinnedIndices(const QHash<QString, int>& pins) { m_pinned = pins; }
    void applyPins();

    // Copy lock + pin metadata (not the plugin entries) from `other`. The reorder
    // primitives that rebuild the list wholesale (LOOT sort / reconcile) use this
    // so the manual-control state survives the rebuild.
    void copyOrderState(const PluginList& other);

    QString toPluginsTxt() const;
    static PluginList fromPluginsTxt(const QString& txt);

    bool saveToFile(const QString& path) const;
    // Writes loadorder.txt: every plugin filename in list order, one per line,
    // with no prefix (full order, active and inactive alike).
    bool saveLoadOrderToFile(const QString& path) const;
    static PluginList loadFromFile(const QString& path);

private:
    QList<PluginEntry> m_plugins;
    bool m_loadOrderLocked = false;
    QHash<QString, int> m_pinned; // lowercased filename -> pinned load-order index
};

} // namespace solero
