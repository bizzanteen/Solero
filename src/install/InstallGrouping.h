#pragma once
#include "core/ModList.h"

namespace solero {

// Group head for a newly installed file of an already-installed Nexus mod, or
// empty. Resolves through parentId so a third file joins the existing group.
inline QString installGroupParent(const ModList& list, const QString& newEntryId,
                                  const QString& nexusModId) {
    if (nexusModId.isEmpty()) return {}; // no id, no identity - never group by name
    for (int i = 0; i < list.count(); ++i) {
        const ModEntry& e = list.at(i);
        if (e.type != EntryType::Mod) continue;
        if (e.id == newEntryId) continue;
        if (e.nexusModId != nexusModId) continue;
        return e.parentId.isEmpty() ? e.id : e.parentId;
    }
    return {};
}

} // namespace solero
