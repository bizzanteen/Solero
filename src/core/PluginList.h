#pragma once
#include "Types.h"
#include <QString>
#include <QPair>

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

    QString toPluginsTxt() const;
    static PluginList fromPluginsTxt(const QString& txt);

    bool saveToFile(const QString& path) const;
    // Writes loadorder.txt: every plugin filename in list order, one per line,
    // with no prefix (full order, active and inactive alike).
    bool saveLoadOrderToFile(const QString& path) const;
    static PluginList loadFromFile(const QString& path);

private:
    QList<PluginEntry> m_plugins;
};

} // namespace solero
