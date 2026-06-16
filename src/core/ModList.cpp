#include "ModList.h"
#include "FileUtil.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QSet>
#include <algorithm>

namespace solero {

void ModList::append(const ModEntry& entry) {
    m_entries.append(entry);
}

void ModList::remove(const QString& id) {
    m_entries.removeIf([&](const ModEntry& e){ return e.id == id; });
}

void ModList::move(int from, int to) {
    m_entries.move(from, to);
}

void ModList::moveSection(int from, int count, int to) {
    if (count <= 0 || from < 0 || from + count > m_entries.size()) return;
    // Extract the block.
    QList<ModEntry> block;
    block.reserve(count);
    for (int i = 0; i < count; ++i) block.append(m_entries.at(from + i));
    m_entries.remove(from, count);
    // `to` is relative to the list with the block already removed; clamp it.
    if (to < 0) to = 0;
    if (to > m_entries.size()) to = m_entries.size();
    for (int i = 0; i < count; ++i) m_entries.insert(to + i, block.at(i));
}

bool ModList::reorder(QList<int> srcRaws, int dstRaw) {
    const int n = m_entries.size();
    // 1. Sort+dedupe, drop out-of-range.
    std::sort(srcRaws.begin(), srcRaws.end());
    srcRaws.erase(std::unique(srcRaws.begin(), srcRaws.end()), srcRaws.end());
    srcRaws.removeIf([&](int i){ return i < 0 || i >= n; });
    if (srcRaws.isEmpty()) return false;

    QSet<int> srcSet(srcRaws.begin(), srcRaws.end());

    // Snapshot the original order so we can detect whether anything actually moved.
    QList<ModEntry> before = m_entries;

    // 2. Clamp dstRaw.
    if (dstRaw < 0) dstRaw = 0;
    if (dstRaw > n) dstRaw = n;

    // 3. Anchor by identity: first index >= dstRaw not in the src set.
    bool anchorEnd = true;
    QString anchorId;
    for (int i = dstRaw; i < n; ++i) {
        if (!srcSet.contains(i)) { anchorEnd = false; anchorId = m_entries.at(i).id; break; }
    }

    // 4. Copy the block (ascending src order), then remove high->low.
    QList<ModEntry> block;
    block.reserve(srcRaws.size());
    for (int i : srcRaws) block.append(m_entries.at(i));
    for (int k = srcRaws.size() - 1; k >= 0; --k) m_entries.removeAt(srcRaws.at(k));

    // 5. Resolve insertion index against the post-removal list.
    int insertAt = m_entries.size();
    if (!anchorEnd) {
        for (int i = 0; i < m_entries.size(); ++i)
            if (m_entries.at(i).id == anchorId) { insertAt = i; break; }
    }
    for (int i = 0; i < block.size(); ++i) m_entries.insert(insertAt + i, block.at(i));

    // 6. Did the order actually change?
    if (m_entries.size() != before.size()) return true;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id != before.at(i).id) return true;
    return false;
}

QStringList ModList::orderIds() const {
    QStringList ids;
    ids.reserve(m_entries.size());
    for (const auto& e : m_entries) ids << e.id;
    return ids;
}

bool ModList::setOrder(const QStringList& ids) {
    // Build the new order by id. Pull each requested id out of a working copy in
    // request order; whatever's left (ids not mentioned in `ids`) is appended in
    // its original relative order so no entry is ever dropped.
    QList<ModEntry> before = m_entries;
    QList<ModEntry> remaining = m_entries;
    QList<ModEntry> rebuilt;
    rebuilt.reserve(m_entries.size());
    for (const QString& id : ids) {
        for (int i = 0; i < remaining.size(); ++i) {
            if (remaining.at(i).id == id) {
                rebuilt.append(remaining.takeAt(i));
                break;
            }
        }
    }
    // Append any entries the snapshot didn't cover (defensive).
    for (const auto& e : remaining) rebuilt.append(e);

    m_entries = std::move(rebuilt);

    if (m_entries.size() != before.size()) return true;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id != before.at(i).id) return true;
    return false;
}

void ModList::update(const QString& id, const ModEntry& updated) {
    for (auto& e : m_entries) if (e.id == id) { e = updated; return; }
}

void ModList::setEnabled(const QString& id, bool enabled) {
    auto* entry = findById(id);
    if (!entry) return;
    entry->enabled = enabled;
    propagateEnabled(id, enabled);
}

int ModList::childRunCount(int parentRaw) const {
    if (parentRaw < 0 || parentRaw >= m_entries.size()) return 0;
    const auto& parent = m_entries.at(parentRaw);
    if (parent.type != EntryType::Mod) return 0;
    int count = 0;
    for (int i = parentRaw + 1; i < m_entries.size(); ++i) {
        const auto& e = m_entries.at(i);
        if (e.type == EntryType::Mod && e.parentId == parent.id) ++count;
        else break;
    }
    return count;
}

void ModList::groupUnder(const QString& childId, const QString& parentId) {
    if (childId.isEmpty() || parentId.isEmpty() || childId == parentId) return;
    int parentRaw = -1, childRaw = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == parentId) parentRaw = i;
        if (m_entries.at(i).id == childId)  childRaw  = i;
    }
    if (parentRaw < 0 || childRaw < 0) return;
    if (m_entries.at(parentRaw).type != EntryType::Mod) return;
    // Only a plain Mod can be grouped under a parent - never a Separator (it owns
    // a whole section, not a single entry), which would corrupt the section model.
    if (m_entries.at(childRaw).type != EntryType::Mod) return;

    // Re-parent. (childRunCount uses parentId, so set this first.)
    m_entries[childRaw].parentId = parentId;

    // Target raw index: end of the parent's contiguous child run. childRunCount
    // already counts the just-re-parented child if it happens to sit in the run,
    // so compute the run EXCLUDING childRaw to find the insertion slot.
    int runEnd = parentRaw + 1;
    while (runEnd < m_entries.size()) {
        if (runEnd == childRaw) { ++runEnd; continue; }
        const auto& e = m_entries.at(runEnd);
        if (e.type == EntryType::Mod && e.parentId == parentId) ++runEnd;
        else break;
    }
    // `runEnd` is the slot just past the last existing child (and the parent).
    // Insert the child there. move() inserts at the destination index after
    // removal, so adjust when the child currently sits before the target.
    int dest = runEnd;
    if (childRaw < dest) dest -= 1;
    if (dest != childRaw) m_entries.move(childRaw, dest);
}

void ModList::ungroup(const QString& childId) {
    if (childId.isEmpty()) return;
    int childRaw = -1;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == childId) { childRaw = i; break; }
    if (childRaw < 0) return;
    const QString parentId = m_entries.at(childRaw).parentId;
    if (parentId.isEmpty()) return; // not a child

    // Locate the (former) parent and the end of its contiguous child block.
    int parentRaw = -1;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == parentId) { parentRaw = i; break; }

    m_entries[childRaw].parentId.clear();

    if (parentRaw < 0) return; // dangling parent: just drop the parentId in place.

    // End of the parent's contiguous child run. The just-cleared child still sits
    // physically inside the run, so skip over it (childRaw) when scanning; the run
    // ends at the first non-child that isn't childRaw.
    int blockEnd = parentRaw + 1;
    while (blockEnd < m_entries.size()) {
        if (blockEnd == childRaw) { ++blockEnd; continue; }
        const auto& e = m_entries.at(blockEnd);
        if (e.type == EntryType::Mod && e.parentId == parentId) ++blockEnd;
        else break;
    }
    // Place the now-top-level mod just after the block.
    int dest = blockEnd;
    if (childRaw < dest) dest -= 1;
    if (dest != childRaw) m_entries.move(childRaw, dest);
}

void ModList::propagateEnabled(const QString& parentId, bool enabled) {
    for (auto& e : m_entries) {
        if (e.parentId == parentId) {
            e.enabled = enabled;
        }
    }
}

ModEntry* ModList::findById(const QString& id) {
    for (auto& e : m_entries) if (e.id == id) return &e;
    return nullptr;
}

const ModEntry* ModList::findById(const QString& id) const {
    for (const auto& e : m_entries) if (e.id == id) return &e;
    return nullptr;
}

ModEntry* ModList::findByNexusId(const QString& nexusModId, const QString& skipId) {
    if (nexusModId.isEmpty()) return nullptr;
    for (auto& e : m_entries)
        if (e.type == EntryType::Mod && e.id != skipId && e.nexusModId == nexusModId)
            return &e;
    return nullptr;
}

ModEntry* ModList::findByNexusFile(const QString& nexusModId,
                                   const QString& nexusFileId,
                                   const QString& skipId) {
    if (nexusModId.isEmpty() || nexusFileId.isEmpty()) return nullptr;
    for (auto& e : m_entries)
        if (e.type == EntryType::Mod && e.id != skipId
            && e.nexusModId == nexusModId && e.nexusFileId == nexusFileId)
            return &e;
    return nullptr;
}

ModEntry* ModList::findByName(const QString& name, const QString& skipId) {
    for (auto& e : m_entries)
        if (e.type == EntryType::Mod && e.id != skipId
            && e.name.compare(name, Qt::CaseInsensitive) == 0)
            return &e;
    return nullptr;
}

const ModEntry* ModList::findCommunityShaders() const {
    for (const auto& e : m_entries) {
        if (e.type != EntryType::Mod) continue;
        if (e.nexusModId == "86492"
            || e.name.compare("Community Shaders", Qt::CaseInsensitive) == 0)
            return &e;
    }
    return nullptr;
}

static QJsonObject entryToJson(const ModEntry& e) {
    QJsonObject o;
    o["type"]            = (e.type == EntryType::Mod) ? "mod" : "separator";
    o["id"]              = e.id;
    o["name"]            = e.name;
    o["enabled"]         = e.enabled;
    o["version"]         = e.version;
    o["nexusModId"]      = e.nexusModId;
    o["nexusFileId"]     = e.nexusFileId;
    o["parentId"]        = e.parentId;
    o["color"]           = e.color;
    o["icon"]            = e.icon;
    o["collapsed"]       = e.collapsed;
    o["separatorLevel"]  = e.separatorLevel;
    o["hasFomodChoices"] = e.hasFomodChoices;
    o["isFomod"]         = e.isFomod;
    o["fomodStatus"]     = e.fomodStatus;
    o["isOutputMod"]     = e.isOutputMod;
    // isManagedCache is no longer written - the cache is profile-level state now
    // (Profile::shaderCache). entryFromJson still READS it for one-time migration.
    o["sourceArchive"]   = e.sourceArchive;
    o["stagingFolder"]   = e.stagingFolder;
    o["note"]            = e.note;
    QJsonArray tags;
    for (const auto& t : e.tags) tags.append(t);
    o["tags"] = tags;
    return o;
}

static ModEntry entryFromJson(const QJsonObject& o) {
    ModEntry e;
    e.type            = (o["type"].toString() == "separator") ? EntryType::Separator : EntryType::Mod;
    e.id              = o["id"].toString();
    e.name            = o["name"].toString();
    e.enabled         = o["enabled"].toBool(true);
    e.version         = o["version"].toString();
    e.nexusModId      = o["nexusModId"].toString();
    e.nexusFileId     = o["nexusFileId"].toString();
    e.parentId        = o["parentId"].toString();
    e.color           = o["color"].toString();
    e.icon            = o["icon"].toString();
    e.collapsed       = o["collapsed"].toBool(false);
    e.separatorLevel  = o["separatorLevel"].toInt(0); // absent in older files -> 0 (top-level)
    e.hasFomodChoices = o["hasFomodChoices"].toBool(false);
    e.isFomod         = o["isFomod"].toBool(false);     // absent in older files -> false
    e.fomodStatus     = o["fomodStatus"].toString();    // absent in older files -> empty
    e.isOutputMod     = o["isOutputMod"].toBool(false);
    e.isManagedCache  = o["isManagedCache"].toBool(false); // absent in older files -> false
    e.sourceArchive   = o["sourceArchive"].toString();
    e.stagingFolder   = o["stagingFolder"].toString(); // absent in older files -> empty (backfilled by migration)
    e.note            = o["note"].toString(); // absent in older files -> empty
    for (const auto& t : o["tags"].toArray()) e.tags.append(t.toString());
    return e;
}

QJsonDocument ModList::toJson() const {
    QJsonArray arr;
    for (const auto& e : m_entries) arr.append(entryToJson(e));
    return QJsonDocument(arr);
}

ModList ModList::fromJson(const QJsonDocument& doc) {
    ModList list;
    for (const auto& v : doc.array()) list.append(entryFromJson(v.toObject()));
    return list;
}

bool ModList::saveToFile(const QString& path) const {
    return atomicWrite(path, toJson().toJson(QJsonDocument::Indented));
}

ModList ModList::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return fromJson(QJsonDocument::fromJson(f.readAll()));
}

} // namespace solero
