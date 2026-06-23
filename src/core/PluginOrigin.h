#pragma once
#include <QHash>
#include <QString>
#include <QStringList>

namespace solero::PluginOrigin {

// Build a reverse index: plugin filename (lowercased) -> the mod ids that ship a
// plugin of that name. `orderedModIds` must be in LOW->HIGH priority order (mod-
// list order: later = higher priority = conflict winner), so each result list is
// likewise ordered low->high and its last entry is the winning provider.
// `pluginsByMod` maps each mod id to its staged plugin filenames.
inline QHash<QString, QStringList> buildIndex(
        const QStringList& orderedModIds,
        const QHash<QString, QStringList>& pluginsByMod) {
    QHash<QString, QStringList> out;
    for (const QString& modId : orderedModIds)
        for (const QString& fn : pluginsByMod.value(modId))
            out[fn.toLower()].append(modId);
    return out;
}

// The winning (highest-priority) provider of a plugin, or empty if none.
inline QString winner(const QStringList& providers) {
    return providers.isEmpty() ? QString() : providers.last();
}

} // namespace solero::PluginOrigin
