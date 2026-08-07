#pragma once
#include "nexus/NexusApi.h"
#include "core/VersionUtil.h"
#include <QSet>

namespace solero {

// Strip a Nexus file name down to its identity: no extension, no trailing
// "-<modId>-<version>-<timestamp>" groups, no punctuation.
// "Cool Armour PBR Textures-12345-1-2-1700000000.7z" -> "coolarmourpbrtextures"
inline QString normalizeNexusFileName(const QString& fileName) {
    QString s = fileName;
    const int dot = s.lastIndexOf('.');
    if (dot > 0) s = s.left(dot);
    // Only whole trailing groups that are pure digits, so "4K" or "SE" survive.
    QStringList parts = s.split('-');
    while (parts.size() > 1) {
        const QString last = parts.last().trimmed();
        bool numeric = !last.isEmpty();
        for (const QChar& c : last) if (!c.isDigit()) { numeric = false; break; }
        if (!numeric) break;
        parts.removeLast();
    }
    s = parts.join('-');
    QString out;
    for (const QChar& c : s) if (c.isLetterOrNumber()) out += c.toLower();
    return out;
}

// The file that should be offered as an update for installedFileId, or empty.
//
// A Nexus page hosts several files at their own versions - a main file at 5.0 and
// a texture pack at 1.2 - so the page's newest main file is not this file's
// update. Follows the author's file_updates chain, and falls back to a same-name,
// same-category file only when there is no chain at all.
inline QString nexusSuccessorFileId(const QList<NexusApi::NexusFile>& files,
                                    const QList<NexusApi::FileUpdate>& updates,
                                    const QString& installedFileId) {
    if (installedFileId.isEmpty()) return {};

    auto fileById = [&](const QString& id) -> const NexusApi::NexusFile* {
        for (const auto& f : files) if (f.fileId == id) return &f;
        return nullptr;
    };

    // Walk the chain as far as it goes; a page can carry several hops.
    QString cur = installedFileId;
    QSet<QString> seen{cur};
    forever {
        QString next;
        for (const auto& u : updates)
            if (u.oldFileId == cur) { next = u.newFileId; break; }
        if (next.isEmpty() || seen.contains(next)) break;
        seen.insert(next);
        cur = next;
    }
    if (cur != installedFileId) return fileById(cur) ? cur : QString();

    // No chain. Match by name within the same category, so a texture pack is never
    // offered the main file. An installed file the page no longer lists gives us
    // nothing to match on.
    const NexusApi::NexusFile* installed = fileById(installedFileId);
    if (!installed) return {};
    const QString key = normalizeNexusFileName(installed->name);
    if (key.isEmpty()) return {};

    const NexusApi::NexusFile* best = nullptr;
    for (const auto& f : files) {
        if (f.fileId == installedFileId) continue;
        if (f.category.compare(installed->category, Qt::CaseInsensitive) != 0) continue;
        if (normalizeNexusFileName(f.name) != key) continue;
        if (f.version.isEmpty() || !isVersionNewer(installed->version, f.version)) continue;
        if (!best || isVersionNewer(best->version, f.version)) best = &f;
    }
    return best ? best->fileId : QString();
}

} // namespace solero
