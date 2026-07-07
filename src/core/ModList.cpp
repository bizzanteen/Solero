#include "ModList.h"
#include "FileUtil.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QSet>
#include <algorithm>

namespace solero {

void ModList::rebuildIndex() {
    m_idIndex.clear();
    m_idIndex.reserve(m_entries.size());
    // Guard so the first occurrence of a (defensively) duplicated id wins, matching
    // the linear scan's "return the first match" behaviour.
    for (int i = 0; i < m_entries.size(); ++i)
        if (!m_idIndex.contains(m_entries.at(i).id))
            m_idIndex.insert(m_entries.at(i).id, i);
}

void ModList::append(const ModEntry& entry) {
    // Patch incrementally so bulk loads (fromJson: one append per entry) stay O(n).
    // Keep first-match semantics: only index the first occurrence of a duplicate id.
    if (!m_idIndex.contains(entry.id)) m_idIndex.insert(entry.id, m_entries.size());
    m_entries.append(entry);
}

void ModList::remove(const QString& id) {
    m_entries.removeIf([&](const ModEntry& e){ return e.id == id; });
    rebuildIndex();
}

void ModList::move(int from, int to) {
    m_entries.move(from, to);
    rebuildIndex();
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
    rebuildIndex();
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
    rebuildIndex();

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
    rebuildIndex();

    if (m_entries.size() != before.size()) return true;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id != before.at(i).id) return true;
    return false;
}

void ModList::update(const QString& id, const ModEntry& updated) {
    // Position is unchanged, but the replacement entry's id could differ, so rebuild.
    for (auto& e : m_entries) if (e.id == id) { e = updated; rebuildIndex(); return; }
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

bool ModList::groupUnder(const QString& childId, const QString& parentId) {
    if (childId.isEmpty() || parentId.isEmpty() || childId == parentId) return false;
    int parentRaw = -1, childRaw = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == parentId) parentRaw = i;
        if (m_entries.at(i).id == childId)  childRaw  = i;
    }
    if (parentRaw < 0 || childRaw < 0) return false;
    if (m_entries.at(parentRaw).type != EntryType::Mod) return false;
    // Only a plain Mod can be grouped under a parent - never a Separator (it owns
    // a whole section, not a single entry), which would corrupt the section model.
    if (m_entries.at(childRaw).type != EntryType::Mod) return false;

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
    const bool moved = (dest != childRaw);
    if (moved) m_entries.move(childRaw, dest);
    rebuildIndex();
    return moved;
}

bool ModList::ungroup(const QString& childId) {
    if (childId.isEmpty()) return false;
    int childRaw = -1;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == childId) { childRaw = i; break; }
    if (childRaw < 0) return false;
    const QString parentId = m_entries.at(childRaw).parentId;
    if (parentId.isEmpty()) return false; // not a child

    // Locate the (former) parent and the end of its contiguous child block.
    int parentRaw = -1;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == parentId) { parentRaw = i; break; }

    m_entries[childRaw].parentId.clear();

    if (parentRaw < 0) return false; // dangling parent: just drop the parentId in place.

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
    const bool moved = (dest != childRaw);
    if (moved) m_entries.move(childRaw, dest);
    rebuildIndex();
    return moved;
}

void ModList::normalizeGroups() {
    // 1. Resolve illegal parents: missing / non-Mod / parent-is-itself-a-child.
    for (auto& e : m_entries) {
        if (e.parentId.isEmpty()) continue;
        const ModEntry* parent = findById(e.parentId);
        if (!parent || parent->type != EntryType::Mod) {
            e.parentId.clear();              // orphan -> top-level
        } else if (!parent->parentId.isEmpty()) {
            e.parentId = parent->parentId;   // flatten 2-level nesting
        }
    }
    // 2. Rebuild contiguous in a single O(n) pass: bucket child indices
    //    by parentId (preserving their existing relative order) and record the
    //    top-level order, then emit each top-level entry followed by its children.
    //    Children are never emitted at top level.
    QHash<QString, QList<int>> childrenOf;
    QList<int> topLevel;
    topLevel.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto& e = m_entries.at(i);
        if (e.parentId.isEmpty()) topLevel.append(i);
        else childrenOf[e.parentId].append(i);
    }
    QList<ModEntry> out;
    out.reserve(m_entries.size());
    for (int ti : topLevel) {
        const ModEntry& e = m_entries.at(ti);
        out.append(e);
        if (e.type == EntryType::Mod) {
            auto it = childrenOf.constFind(e.id);
            if (it != childrenOf.constEnd())
                for (int ci : it.value()) out.append(m_entries.at(ci));
        }
    }
    m_entries = std::move(out);
    rebuildIndex();
}

void ModList::propagateEnabled(const QString& parentId, bool enabled) {
    for (auto& e : m_entries) {
        if (e.parentId == parentId) {
            e.enabled = enabled;
        }
    }
}

ModEntry* ModList::findById(const QString& id) {
    auto it = m_idIndex.constFind(id); // O(1) via m_idIndex
    return it == m_idIndex.constEnd() ? nullptr : &m_entries[it.value()];
}

const ModEntry* ModList::findById(const QString& id) const {
    auto it = m_idIndex.constFind(id); // O(1) via m_idIndex
    return it == m_idIndex.constEnd() ? nullptr : &m_entries.at(it.value());
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

static void syncVariantMirrors(ModEntry& e) {
    if (e.activeVariant < 0 || e.activeVariant >= e.variants.size()) return;
    const ModVariant& v = e.variants[e.activeVariant];
    e.version = v.version;             e.nexusFileId = v.nexusFileId;
    e.stagingFolder = v.stagingFolder; e.sourceArchive = v.sourceArchive;
    e.hasFomodChoices = v.hasFomodChoices;
}

bool ModList::keepBothAddVariant(const QString& id, const ModVariant& v) {
    ModEntry* e = findById(id);
    if (!e || e->type != EntryType::Mod || v.stagingFolder.isEmpty()) return false;
    if (e->variants.isEmpty()) {
        // Snapshot the current single install as variant 0.
        e->variants.append({e->version, e->nexusFileId, e->stagingFolder,
                            e->sourceArchive, e->hasFomodChoices});
    }
    e->variants.append(v);
    e->activeVariant = e->variants.size() - 1;
    syncVariantMirrors(*e);
    return true;
}

bool ModList::setActiveVariant(const QString& id, int index) {
    ModEntry* e = findById(id);
    if (!e || index < 0 || index >= e->variants.size()) return false;
    e->activeVariant = index;
    syncVariantMirrors(*e);
    return true;
}

bool ModList::replaceActiveVersion(const QString& id, const ModVariant& v,
                                   QString* retiredFolder) {
    ModEntry* e = findById(id);
    if (!e || e->type != EntryType::Mod) return false;
    const QString old = e->stagingFolder;
    if (e->variants.isEmpty()) {
        e->version = v.version; e->nexusFileId = v.nexusFileId;
        e->stagingFolder = v.stagingFolder; e->sourceArchive = v.sourceArchive;
        e->hasFomodChoices = v.hasFomodChoices;
    } else {
        e->variants[e->activeVariant] = v;
        syncVariantMirrors(*e);
    }
    if (retiredFolder) *retiredFolder = (old == v.stagingFolder) ? QString() : old;
    return true;
}

int ModList::variantIndexByFileId(const QString& id, const QString& fileId) const {
    if (fileId.isEmpty()) return -1;
    const ModEntry* e = findById(id);
    if (!e || e->variants.isEmpty()) return -1;
    for (int i = 0; i < e->variants.size(); ++i)
        if (e->variants[i].nexusFileId == fileId) return i;
    return -1;
}

bool ModList::updateVariant(const QString& id, int index, const ModVariant& v) {
    ModEntry* e = findById(id);
    if (!e || index < 0 || index >= e->variants.size()) return false;
    e->variants[index] = v;
    if (index == e->activeVariant) syncVariantMirrors(*e);
    return true;
}

void ModList::normalizeVariants() {
    for (auto& e : m_entries) {
        if (e.variants.isEmpty()) { e.activeVariant = -1; continue; }
        e.activeVariant = qBound(0, e.activeVariant, int(e.variants.size()) - 1);
        syncVariantMirrors(e);
    }
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
    if (!e.variants.isEmpty()) {
        QJsonArray varArr;
        for (const auto& v : e.variants) {
            QJsonObject vo;
            vo["version"]        = v.version;
            vo["nexusFileId"]    = v.nexusFileId;
            vo["stagingFolder"]  = v.stagingFolder;
            vo["sourceArchive"]  = v.sourceArchive;
            vo["hasFomodChoices"] = v.hasFomodChoices;
            varArr.append(vo);
        }
        o["variants"]     = varArr;
        o["activeVariant"] = e.activeVariant;
    }
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
    if (o.contains("variants")) {
        for (const auto& val : o["variants"].toArray()) {
            const QJsonObject vo = val.toObject();
            e.variants.append({vo["version"].toString(),
                               vo["nexusFileId"].toString(),
                               vo["stagingFolder"].toString(),
                               vo["sourceArchive"].toString(),
                               vo["hasFomodChoices"].toBool(false)});
        }
        e.activeVariant = o["activeVariant"].toInt(-1);
    }
    // absent "variants" key -> e.variants stays empty, e.activeVariant stays -1
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
    ModList list = fromJson(QJsonDocument::fromJson(f.readAll()));
    list.normalizeGroups();    // heal any persisted group-invariant corruption on load
    list.normalizeVariants();  // clamp activeVariant into range after load
    return list;
}

} // namespace solero
