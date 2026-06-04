#include "ModList.h"
#include "FileUtil.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>

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

void ModList::update(const QString& id, const ModEntry& updated) {
    for (auto& e : m_entries) if (e.id == id) { e = updated; return; }
}

void ModList::setEnabled(const QString& id, bool enabled) {
    auto* entry = findById(id);
    if (!entry) return;
    entry->enabled = enabled;
    propagateEnabled(id, enabled);
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
    o["hasFomodChoices"] = e.hasFomodChoices;
    o["isOutputMod"]     = e.isOutputMod;
    o["sourceArchive"]   = e.sourceArchive;
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
    e.hasFomodChoices = o["hasFomodChoices"].toBool(false);
    e.isOutputMod     = o["isOutputMod"].toBool(false);
    e.sourceArchive   = o["sourceArchive"].toString();
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
