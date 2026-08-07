#pragma once
#include <QHash>
#include <QString>
#include <QStringList>

namespace solero::PluginOrigin {

// Reverse index: lowercased plugin filename -> the mod ids shipping it.
// orderedModIds must be low->high priority, so each result is too and its last
// entry is the winning provider.
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
