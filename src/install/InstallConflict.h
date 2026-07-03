#pragma once
#include "core/ModList.h"
#include "core/VersionUtil.h"

namespace solero {

enum class InstallConflictKind { NoConflict, SameFile, VersionConflict, SiblingFile };
struct InstallConflict { InstallConflictKind kind = InstallConflictKind::NoConflict;
                         QString targetEntryId; };

// Classify an incoming Nexus archive (sidecar identity) against the mod list.
// Non-const ModList& only because find* helpers are non-const.
inline InstallConflict classifyInstallConflict(ModList& ml, const QString& modId,
                                               const QString& fileId,
                                               const QString& version) {
    if (modId.isEmpty()) return {};
    if (ModEntry* same = ml.findByNexusFile(modId, fileId))
        return {InstallConflictKind::SameFile, same->id};
    // A fileId matching a non-active variant is the same file too.
    for (const auto& e : ml)
        if (e.type == EntryType::Mod && e.nexusModId == modId)
            for (const auto& v : e.variants)
                if (!fileId.isEmpty() && v.nexusFileId == fileId)
                    return {InstallConflictKind::SameFile, e.id};
    // Prefer a top-level entry with this modId over a grouped child, and count how
    // many entries share the modId.
    ModEntry* target = nullptr;
    ModEntry* firstTop = nullptr;
    int modIdCount = 0;
    for (auto& e : ml.entries())
        if (e.type == EntryType::Mod && e.nexusModId == modId) {
            ++modIdCount;
            if (!target) target = &e;
            if (!firstTop && e.parentId.isEmpty()) firstTop = &e;
        }
    if (firstTop) target = firstTop;
    if (!target) return {};
    // A grouped multi-file mod (more than one entry shares the modId, e.g. a main
    // file + an NG DLL) must never hit the version dialog - Keep Both would attach
    // a sibling file as a bogus "version". Attach it silently to the top-level entry.
    // VersionConflict only fires when exactly one entry owns the modId.
    if (modIdCount > 1)
        return {InstallConflictKind::SiblingFile, target->id};
    const QString a = normalizeVersion(version), b = normalizeVersion(target->version);
    if (!a.isEmpty() && !b.isEmpty() && a != b)
        return {InstallConflictKind::VersionConflict, target->id};
    return {InstallConflictKind::SiblingFile, target->id};
}

} // namespace solero
