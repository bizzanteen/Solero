#include "ProfileManifest.h"
#include "core/Profile.h"
#include "core/ProfileManager.h"
#include "core/FileUtil.h"
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QUuid>
#include <QSet>

namespace solero {

static const QString kFormat = QStringLiteral("solero-profile/1");

// Read the `steps` array out of a mod's fomod-choices.json (the same shape
// onInstallMod / FomodScanner write: { installer_version, installed_at, steps:[…] }).
// Returns an empty array when the file is absent or has no steps.
static QJsonArray readFomodSteps(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().value("steps").toArray();
}

QJsonDocument ProfileManifest::toJson(const Profile& profile, const QString& fomodChoicesDir) {
    const ModList& list = profile.modList();

    QJsonArray mods;
    for (int i = 0; i < list.count(); ++i) {
        const ModEntry& e = list.at(i);
        QJsonObject o;
        o["type"]    = (e.type == EntryType::Mod) ? "mod" : "separator";
        o["name"]    = e.name;
        o["enabled"] = e.enabled;

        if (e.type == EntryType::Separator) {
            o["color"]     = e.color;
            o["icon"]      = e.icon;
            o["collapsed"] = e.collapsed;
        } else {
            o["nexusModId"]  = e.nexusModId;
            o["nexusFileId"] = e.nexusFileId;
            o["version"]     = e.version;
            if (!e.tags.isEmpty()) {
                QJsonArray tags;
                for (const QString& t : e.tags) tags.append(t);
                o["tags"] = tags;
            }
            const QJsonArray steps = readFomodSteps(fomodChoicesDir + "/" + e.id + ".json");
            if (!steps.isEmpty()) o["fomodChoices"] = steps;
        }

        // Portable parent encoding: the ORDINAL position of the parent entry in
        // this same mods array (never the raw UUID - ids differ across machines).
        if (!e.parentId.isEmpty()) {
            for (int j = 0; j < list.count(); ++j) {
                if (list.at(j).id == e.parentId) { o["parentIndex"] = j; break; }
            }
        }
        mods.append(o);
    }

    QJsonArray plugins;
    const PluginList& pl = profile.pluginList();
    for (int i = 0; i < pl.count(); ++i) {
        QJsonObject po;
        po["name"]    = pl.at(i).filename;
        po["enabled"] = pl.at(i).enabled;
        plugins.append(po);
    }

    QJsonObject root;
    root["format"]          = kFormat;
    root["exportedProfile"] = profile.name();
    root["mods"]            = mods;
    root["pluginOrder"]     = plugins;
    return QJsonDocument(root);
}

bool ProfileManifest::exportToFile(const Profile& profile, const QString& path,
                                   const QString& fomodChoicesDir) {
    return atomicWrite(path, toJson(profile, fomodChoicesDir).toJson(QJsonDocument::Indented));
}

// Match a manifest mod against the pool, mirroring ModList::findByNexusId /
// findByName semantics but skipping pool mods already consumed by an earlier
// entry (so two manifest mods never collapse onto one installed mod). Returns a
// pointer into `pool`, or nullptr.
static ModEntry* matchInPool(ModList& pool, const QString& nexusModId,
                             const QString& nexusFileId, const QString& name,
                             const QSet<QString>& used) {
    // 1. nexusModId + nexusFileId (both present, exact).
    if (!nexusModId.isEmpty() && !nexusFileId.isEmpty()) {
        for (int i = 0; i < pool.count(); ++i) {
            const ModEntry& e = pool.at(i);
            if (e.type == EntryType::Mod && !used.contains(e.id)
                && e.nexusModId == nexusModId && e.nexusFileId == nexusFileId)
                return pool.findById(e.id);
        }
    }
    // 2. nexusModId.
    if (!nexusModId.isEmpty()) {
        for (int i = 0; i < pool.count(); ++i) {
            const ModEntry& e = pool.at(i);
            if (e.type == EntryType::Mod && !used.contains(e.id) && e.nexusModId == nexusModId)
                return pool.findById(e.id);
        }
    }
    // 3. case-insensitive name.
    for (int i = 0; i < pool.count(); ++i) {
        const ModEntry& e = pool.at(i);
        if (e.type == EntryType::Mod && !used.contains(e.id)
            && e.name.compare(name, Qt::CaseInsensitive) == 0)
            return pool.findById(e.id);
    }
    return nullptr;
}

ManifestBuildResult ProfileManifest::build(const QJsonDocument& manifest, ModList pool) {
    ManifestBuildResult r;
    const QJsonObject root = manifest.object();
    r.exportedProfile = root["exportedProfile"].toString();
    const QJsonArray mods = root["mods"].toArray();

    // manifest index -> new entry id (empty when an entry was skipped/unmatched).
    QList<QString> newIdByIndex;
    newIdByIndex.reserve(mods.size());
    QSet<QString> used; // consumed pool mod ids

    // Pass 1: match + append in manifest order.
    for (const QJsonValue& v : mods) {
        const QJsonObject o = v.toObject();
        if (o["type"].toString() == "separator") {
            ModEntry sep;
            sep.type      = EntryType::Separator;
            sep.name      = o["name"].toString();
            sep.enabled   = o["enabled"].toBool(true);
            sep.color     = o["color"].toString();
            sep.icon      = o["icon"].toString();
            sep.collapsed = o["collapsed"].toBool(false);
            sep.id        = QUuid::createUuid().toString(QUuid::WithoutBraces);
            r.modList.append(sep);
            newIdByIndex.append(sep.id);
            ++r.separators;
            continue;
        }

        const QString name   = o["name"].toString();
        const QString modId  = o["nexusModId"].toString();
        const QString fileId = o["nexusFileId"].toString();

        ModEntry* match = matchInPool(pool, modId, fileId, name, used);
        if (!match) {
            r.missing.append({ name, modId, o["version"].toString() });
            newIdByIndex.append(QString()); // skipped - not created
            continue;
        }
        used.insert(match->id);

        // Keep the installed mod's id + staging-bound metadata (sourceArchive, …);
        // overlay the manifest's profile-level state.
        ModEntry m = *match;
        m.enabled = o["enabled"].toBool(true);
        m.parentId.clear(); // re-linked in pass 2
        if (o.contains("tags")) {
            m.tags.clear();
            for (const QJsonValue& t : o["tags"].toArray()) m.tags.append(t.toString());
        }
        const QJsonArray steps = o["fomodChoices"].toArray();
        if (!steps.isEmpty()) {
            m.isFomod         = true;
            m.hasFomodChoices = true;
            r.fomodChoices.insert(m.id, steps);
        }
        r.modList.append(m);
        newIdByIndex.append(m.id);
        ++r.modsMatched;
    }

    // Pass 2: re-link parent groups via the portable ordinal index. Manifest order
    // is preserved, so a matched child still sits contiguously after its matched
    // parent; if the parent was unmatched (skipped), the child becomes top-level.
    for (int i = 0; i < mods.size(); ++i) {
        const QJsonObject o = mods.at(i).toObject();
        if (!o.contains("parentIndex")) continue;
        const QString childId = newIdByIndex.at(i);
        if (childId.isEmpty()) continue;                 // child unmatched
        const int pidx = o["parentIndex"].toInt(-1);
        if (pidx < 0 || pidx >= newIdByIndex.size()) continue;
        const QString parentId = newIdByIndex.at(pidx);
        if (parentId.isEmpty()) continue;                // parent unmatched -> top-level
        if (ModEntry* child = r.modList.findById(childId)) child->parentId = parentId;
    }

    // Plugin load order - build a fresh list (mirrors Mo2Importer::applyPlugins),
    // deriving the master/light flags from the filename suffix.
    for (const QJsonValue& pv : root["pluginOrder"].toArray()) {
        const QJsonObject po = pv.toObject();
        PluginEntry pe;
        pe.filename = po["name"].toString();
        if (pe.filename.isEmpty()) continue;
        pe.enabled  = po["enabled"].toBool(true);
        pe.isMaster = pe.filename.endsWith(".esm", Qt::CaseInsensitive);
        pe.isLight  = pe.filename.endsWith(".esl", Qt::CaseInsensitive);
        r.pluginList.append(pe);
    }

    return r;
}

ProfileImportResult ProfileManifest::importFile(const QString& manifestPath,
                                                ProfileManager& profiles,
                                                const ModList& pool,
                                                const QString& fomodChoicesDir) {
    ProfileImportResult r;

    QFile f(manifestPath);
    if (!f.open(QIODevice::ReadOnly)) { r.errorMessage = "Could not open manifest file."; return r; }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        r.errorMessage = "Manifest is not valid JSON."; return r;
    }
    // Forward-compatible: accept any "solero-profile/*" version (unknown keys ignored).
    if (!doc.object()["format"].toString().startsWith("solero-profile/")) {
        r.errorMessage = "Not a Solero profile manifest."; return r;
    }

    ManifestBuildResult b = build(doc, pool);

    // Disambiguate the new profile name (mirrors Mo2Importer's pattern).
    QString name = b.exportedProfile.trimmed();
    if (name.isEmpty()) name = "Imported Profile";
    const QStringList existing = profiles.profileNames();
    auto taken = [&](const QString& n) { return existing.contains(n); };
    if (taken(name)) {
        const QString base = name + " (imported)";
        name = base;
        int n = 2;
        while (taken(name)) { name = base + " " + QString::number(n); ++n; }
    }

    if (!profiles.createProfile(name)) {
        r.errorMessage = "Could not create profile '" + name + "'."; return r;
    }
    Profile* p = profiles.loadProfile(name);
    if (!p) { r.errorMessage = "Could not load created profile."; return r; }
    p->modList()    = b.modList;
    p->pluginList() = b.pluginList;
    p->save();

    // Back-fill fomod-choices.json for matched FOMOD mods so the patch wizard /
    // reinstall flow can see the recovered choices (same shape onInstallMod writes).
    if (!b.fomodChoices.isEmpty()) {
        QDir().mkpath(fomodChoicesDir);
        for (auto it = b.fomodChoices.cbegin(); it != b.fomodChoices.cend(); ++it) {
            QJsonObject fr;
            fr["installer_version"] = "1.0";
            fr["installed_at"]      = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            fr["imported"]          = true;
            fr["steps"]             = it.value();
            atomicWrite(fomodChoicesDir + "/" + it.key() + ".json",
                        QJsonDocument(fr).toJson(QJsonDocument::Indented));
        }
    }

    r.success     = true;
    r.profileName = name;
    r.modsMatched = b.modsMatched;
    r.separators  = b.separators;
    r.missing     = b.missing;
    return r;
}

} // namespace solero
