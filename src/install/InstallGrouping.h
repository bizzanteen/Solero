#pragma once
#include "core/ModList.h"

namespace solero {

// Pick the group head a newly installed mod should be nested under, or empty when
// it is not another file of an already-installed mod.
//
// A Nexus page often ships several files (a main file plus PBR textures, a patch,
// an NG DLL). Each installs as its own entry, and they belong together: the extra
// file becomes a child of the main file's group, named after its own archive, so
// the list reads "Cool Armour" > "Cool Armour PBR Textures".
//
// The head is the matching entry itself when it is top-level, or its parent when
// it is already a child, so a third file joins the existing group rather than
// starting a nested one. An entry with no Nexus id never groups: identity is what
// makes two entries the same mod, and a shared name is not identity.
inline QString installGroupParent(const ModList& list, const QString& newEntryId,
                                  const QString& nexusModId) {
    if (nexusModId.isEmpty()) return {};
    // A new mod is appended at the end of the list, so it lands in the trailing
    // section. Only group it under a candidate in that same section.
    int lastSeparatorRaw = -1;
    for (int i = 0; i < list.count(); ++i)
        if (list.at(i).type == EntryType::Separator) lastSeparatorRaw = i;
    for (int i = 0; i < list.count(); ++i) {
        const ModEntry& e = list.at(i);
        if (e.type != EntryType::Mod) continue;
        if (e.id == newEntryId) continue;
        if (e.nexusModId != nexusModId) continue;
        if (i <= lastSeparatorRaw) continue;
        return e.parentId.isEmpty() ? e.id : e.parentId;
    }
    return {};
}

} // namespace solero
