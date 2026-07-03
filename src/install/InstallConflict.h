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
    // Prefer a top-level entry with this modId over a grouped child.
    ModEntry* target = nullptr;
    for (auto& e : ml.entries())
        if (e.type == EntryType::Mod && e.nexusModId == modId) {
            if (e.parentId.isEmpty()) { target = &e; break; }
            if (!target) target = &e;
        }
    if (!target) return {};
    const QString a = normalizeVersion(version), b = normalizeVersion(target->version);
    if (!a.isEmpty() && !b.isEmpty() && a != b)
        return {InstallConflictKind::VersionConflict, target->id};
    return {InstallConflictKind::SiblingFile, target->id};
}

} // namespace solero
