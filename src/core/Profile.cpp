#include "Profile.h"
#include "ShaderCache.h"
#include "FileUtil.h"
#include "StagingFolder.h"
#include "AppConfig.h"
#include "core/Log.h"
#include <QDir>
#include <QFile>
#include <QSet>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

namespace solero {

Profile::Profile(const QString& name, const QString& rootPath)
    : m_name(name), m_path(rootPath + "/" + name) {}

QString Profile::modlistPath()       const { return m_path + "/modlist.json"; }
QString Profile::pluginsPath()       const { return m_path + "/plugins.txt"; }
QString Profile::skyrimIniPath()     const { return m_path + "/Skyrim.ini"; }
QString Profile::skyrimPrefsPath()   const { return m_path + "/SkyrimPrefs.ini"; }
QString Profile::skyrimCustomPath()  const { return m_path + "/SkyrimCustom.ini"; }
QString Profile::executablesPath()   const { return m_path + "/executables.json"; }
QString Profile::lootUserlistPath()  const { return m_path + "/loot-userlist.yaml"; }
QString Profile::fileRulesPath()     const { return m_path + "/filerules.json"; }
QString Profile::loadOrderStatePath() const { return m_path + "/loadorder-state.json"; }
QString Profile::shaderCachePath()    const { return m_path + "/shadercache.json"; }

bool Profile::save() const {
    QDir().mkpath(m_path);
    if (!m_modList.saveToFile(modlistPath())) {
        qCWarning(lcProfile) << "save:" << m_name << "modlist write failed" << modlistPath();
        return false;
    }
    if (!m_pluginList.saveToFile(pluginsPath())) {
        qCWarning(lcProfile) << "save:" << m_name << "plugins write failed" << pluginsPath();
        return false;
    }
    if (!saveExecutables()) {
        qCWarning(lcProfile) << "save:" << m_name << "executables write failed" << executablesPath();
        return false;
    }
    if (!saveFileRules()) {
        qCWarning(lcProfile) << "save:" << m_name << "file rules write failed" << fileRulesPath();
        return false;
    }
    if (!saveShaderCache()) {
        qCWarning(lcProfile) << "save:" << m_name << "shader cache write failed" << shaderCachePath();
        return false;
    }
    const bool ok = saveLoadOrderState();
    if (!ok)
        qCWarning(lcProfile) << "save:" << m_name << "load-order state write failed" << loadOrderStatePath();
    else
        qCInfo(lcProfile) << "saved profile" << m_name << "->" << m_path;
    return ok;
}

bool Profile::saveModListOnly() const {
    QDir().mkpath(m_path);
    return m_modList.saveToFile(modlistPath());
}

bool Profile::saveShaderCache() const {
    // Only persist when active; otherwise remove a stale file so an un-managed
    // profile has no shadercache.json lying around.
    if (!m_shaderCache.active()) {
        QFile::remove(shaderCachePath());
        return true;
    }
    QJsonObject foldersObj;
    for (auto it = m_shaderCache.folders.cbegin(); it != m_shaderCache.folders.cend(); ++it)
        foldersObj.insert(it.key(), it.value());
    QJsonObject root;
    root["managed"] = m_shaderCache.managed;
    root["folders"] = foldersObj;
    return atomicWrite(shaderCachePath(), QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool Profile::loadShaderCache() {
    m_shaderCache = {};
    QFile f(shaderCachePath());
    if (!f.open(QIODevice::ReadOnly)) return false; // missing == not managed
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    m_shaderCache.managed = root["managed"].toBool(false);
    if (root.contains("folders")) {
        // New schema: {"managed": bool, "folders": {key: folder, ...}}
        const auto fobj = root["folders"].toObject();
        for (auto it = fobj.constBegin(); it != fobj.constEnd(); ++it)
            m_shaderCache.folders.insert(it.key(), it.value().toString());
    } else {
        // Legacy schema migration: {"managed": bool, "stagingFolder": str}
        // m_modList must already be loaded before this is called (see Profile::load).
        const QString sf = root["stagingFolder"].toString();
        if (!sf.isEmpty())
            m_shaderCache.folders.insert(activeCacheKey(m_modList), sf);
    }
    return true;
}

bool Profile::migrateManagedCacheEntry() {
    // Legacy profiles stored the managed cache as a hidden mod-list entry. Lift the
    // first such entry into m_shaderCache and drop it from the list. The staged
    // shaders on disk are untouched - only the bookkeeping moves.
    auto& entries = m_modList.entries();
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].type != EntryType::Mod || !entries[i].isManagedCache) continue;
        if (!m_shaderCache.active()) { // don't clobber state already in shadercache.json
            m_shaderCache.managed = entries[i].enabled;
            const QString key = activeCacheKey(m_modList);
            if (!entries[i].stagingFolder.isEmpty())
                m_shaderCache.folders.insert(key, entries[i].stagingFolder);
        }
        m_modList.remove(entries[i].id);
        return true;
    }
    return false;
}

bool Profile::load() {
    if (QFile::exists(modlistPath()))
        m_modList = ModList::loadFromFile(modlistPath());
    if (QFile::exists(pluginsPath()))
        m_pluginList = PluginList::loadFromFile(pluginsPath());
    loadExecutables();
    loadFileRules();
    loadShaderCache();
    loadLoadOrderState(); // after the plugin list - state is applied onto it
    // Lift any legacy isManagedCache mod-list entry into m_shaderCache.
    bool dirty = migrateManagedCacheEntry();
    // Backfill staging-folder names and rename UUID folders on disk.
    dirty = migrateStagingFolders() || dirty;
    if (dirty) save(); // persist migrations (new shadercache.json + cleaned modlist)
    qCInfo(lcProfile) << "loaded profile" << m_name << "from" << m_path
                      << (dirty ? "(migrations persisted)" : "");
    return true;
}

bool Profile::seedExecutablesFrom(
        const QList<Executable>& templateTools,
        const std::function<QString(const Executable&)>& resolveOutputMod) {
    // Never overwrite an existing per-profile setup: bail if we already hold
    // executables in memory. One-time-ness is enforced at the app level (the
    // toolsMigratedToPerProfile flag), so an empty executables.json on disk -
    // left behind by past saves - must not block seeding.
    if (!m_executables.isEmpty())
        return false;

    for (Executable e : templateTools) { // deep copy each template entry
        // Re-resolve the main output mod for this profile. Only entries that
        // actually target an output mod get re-resolved; an empty outputModId
        // (e.g. Overwrite) stays empty.
        if (!e.outputModId.isEmpty())
            e.outputModId = resolveOutputMod(e);
        // extraActions[].outputModId can't be re-resolved here: the resolver is
        // keyed on the main Executable, and the template's per-action output-mod
        // name isn't available in this context. Clear them so seeded actions
        // don't point at the template's (foreign-profile) ids; they're recreated
        // on demand when the action is set up. The main outputModId above is the
        // one that matters for the frictionless-output flow.
        for (auto& a : e.extraActions)
            a.outputModId.clear();
        m_executables.append(e);
    }
    save();
    return true;
}

QString Profile::matchOutputModId(const ModList& modList, const QString& outputModName) {
    if (outputModName.isEmpty()) return QString();
    for (const auto& m : modList.entries()) {
        if (m.type != EntryType::Mod || !m.isOutputMod) continue;
        if (m.name.compare(outputModName, Qt::CaseInsensitive) == 0)
            return m.id;
    }
    return QString();
}

QString Profile::stagingFolderFor(const QString& id) const {
    const ModEntry* e = m_modList.findById(id);
    if (!e) return id; // unknown id: fall back to the id itself (caller built a path)
    return e->stagingFolder.isEmpty() ? id : e->stagingFolder;
}

bool Profile::migrateStagingFolders() {
    const QString stagingDir = AppConfig::instance().stagingDir();
    auto& entries = m_modList.entries();

    // Build the set of folder names already taken (case-insensitive), so newly
    // assigned names don't collide with each other or with pre-set ones.
    QSet<QString> taken;
    for (const auto& e : entries)
        if (e.type == EntryType::Mod && !e.stagingFolder.isEmpty())
            taken.insert(e.stagingFolder.toLower());

    bool changed = false;
    bool backedUp = false; // back up the modlist + start the mapping once, lazily

    for (auto& e : entries) {
        if (e.type != EntryType::Mod) continue;

        const QString uuidPath   = stagingDir + "/" + e.id;
        const bool uuidExists    = !stagingDir.isEmpty() && QDir(uuidPath).exists();
        const bool folderSet     = !e.stagingFolder.isEmpty();
        const bool folderOnDisk  = folderSet && !stagingDir.isEmpty()
                                   && QDir(stagingPathFor(stagingDir, e)).exists();

        // Already migrated: name set and (its folder exists OR there's no UUID
        // folder to migrate from). Skip.
        if (folderSet && (folderOnDisk || !uuidExists))
            continue;

        // Need a name. If one is already set but its folder is missing while the
        // UUID folder still exists, we keep the existing name and just rename.
        QString newFolder = e.stagingFolder;
        if (newFolder.isEmpty()) {
            newFolder = uniqueStagingFolder(sanitizeStagingFolder(e.name), taken);
        }

        if (uuidExists) {
            // If the target name is already on disk (e.g. a partial prior run),
            // pick the next free suffix.
            if (QDir(stagingDir + "/" + newFolder).exists())
                newFolder = uniqueStagingFolder(newFolder, taken);

            // Lazily back up before the first physical rename in this run.
            if (!backedUp) {
                QFile::copy(modlistPath(), modlistPath() + ".bak-stagingmigration");
                backedUp = true;
            }
            if (!QDir().rename(uuidPath, stagingDir + "/" + newFolder)) {
                qWarning() << "migrateStagingFolders: rename failed for" << e.id
                           << "->" << newFolder << "; keeping UUID folder";
                // Keep files: leave stagingFolder pointing at the UUID so the
                // resolver still finds the data on disk.
                if (e.stagingFolder != e.id) { e.stagingFolder = e.id; changed = true; }
                taken.insert(e.id.toLower());
                continue;
            }
            appendMigrationMapping(e.id, newFolder);
        }

        if (e.stagingFolder != newFolder) {
            e.stagingFolder = newFolder;
            changed = true;
        }
        taken.insert(newFolder.toLower());
    }
    return changed;
}

void Profile::appendMigrationMapping(const QString& id, const QString& folder) {
    const QString mapPath = m_path + "/staging-folder-migration.json";
    QJsonObject obj;
    QFile f(mapPath);
    if (f.open(QIODevice::ReadOnly)) {
        obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
    }
    obj.insert(id, folder);
    atomicWrite(mapPath, QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

bool Profile::isFileHidden(const QString& modId, const QString& relPath) const {
    auto it = m_hiddenFiles.constFind(modId);
    return it != m_hiddenFiles.constEnd() && it.value().contains(relPath);
}

void Profile::setFileHidden(const QString& modId, const QString& relPath, bool hidden) {
    if (hidden) {
        m_hiddenFiles[modId].insert(relPath);
    } else {
        auto it = m_hiddenFiles.find(modId);
        if (it == m_hiddenFiles.end()) return;
        it.value().remove(relPath);
        if (it.value().isEmpty()) m_hiddenFiles.erase(it); // keep the map sparse
    }
}

QString Profile::winnerOverride(const QString& relPath) const {
    return m_fileOverrides.value(relPath);
}

void Profile::setWinnerOverride(const QString& relPath, const QString& modId) {
    if (modId.isEmpty()) { m_fileOverrides.remove(relPath); return; }
    m_fileOverrides[relPath] = modId;
}

void Profile::clearWinnerOverride(const QString& relPath) {
    m_fileOverrides.remove(relPath);
}

bool Profile::saveExecutables() const {
    QJsonArray arr;
    for (const auto& e : m_executables) {
        QJsonObject o;
        o["id"]                = e.id;
        o["name"]              = e.name;
        o["binaryPath"]        = e.binaryPath;
        o["workingDir"]        = e.workingDir;
        o["arguments"]         = e.arguments;
        o["runtime"]           = (e.runtime == RuntimeType::Proton) ? "proton" : "native";
        o["protonVersion"]     = e.protonVersion;
        o["winePrefix"]        = e.winePrefix;
        o["runThroughDeployer"]= e.runThroughDeployer;
        o["isPrimary"]         = e.isPrimary;
        o["isCapturingOutput"] = e.isCapturingOutput;
        o["outputModId"]       = e.outputModId;
        o["iconPath"]          = e.iconPath;
        QJsonArray acts;
        for (const auto& a : e.extraActions) {
            QJsonObject ao;
            ao["label"]       = a.label;
            ao["binaryPath"]  = a.binaryPath;
            ao["arguments"]   = a.arguments;
            ao["outputModId"] = a.outputModId;
            acts.append(ao);
        }
        o["extraActions"]      = acts;
        arr.append(o);
    }
    return atomicWrite(executablesPath(), QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

bool Profile::loadExecutables() {
    QFile f(executablesPath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
        auto o = v.toObject();
        Executable e;
        e.id                = o["id"].toString();
        e.name              = o["name"].toString();
        e.binaryPath        = o["binaryPath"].toString();
        e.workingDir        = o["workingDir"].toString();
        e.arguments         = o["arguments"].toString();
        e.runtime           = (o["runtime"].toString() == "proton") ? RuntimeType::Proton : RuntimeType::Native;
        e.protonVersion     = o["protonVersion"].toString();
        e.winePrefix        = o["winePrefix"].toString();
        e.runThroughDeployer= o["runThroughDeployer"].toBool(false);
        e.isPrimary         = o["isPrimary"].toBool(false);
        e.isCapturingOutput = o["isCapturingOutput"].toBool(false);
        e.outputModId       = o["outputModId"].toString();
        e.iconPath          = o["iconPath"].toString();
        for (const auto& av : o["extraActions"].toArray()) {
            auto ao = av.toObject();
            ToolAction a;
            a.label       = ao["label"].toString();
            a.binaryPath  = ao["binaryPath"].toString();
            a.arguments   = ao["arguments"].toString();
            a.outputModId = ao["outputModId"].toString();
            e.extraActions.append(a);
        }
        m_executables.append(e);
    }
    return true;
}

bool Profile::saveFileRules() const {
    QJsonObject hidden;
    for (auto it = m_hiddenFiles.cbegin(); it != m_hiddenFiles.cend(); ++it) {
        if (it.value().isEmpty()) continue;
        QJsonArray paths;
        for (const auto& p : it.value()) paths.append(p);
        hidden.insert(it.key(), paths);
    }
    QJsonObject overrides;
    for (auto it = m_fileOverrides.cbegin(); it != m_fileOverrides.cend(); ++it)
        overrides.insert(it.key(), it.value());

    QJsonObject root;
    root["hiddenFiles"]   = hidden;
    root["fileOverrides"] = overrides;
    return atomicWrite(fileRulesPath(), QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool Profile::loadFileRules() {
    m_hiddenFiles.clear();
    m_fileOverrides.clear();
    QFile f(fileRulesPath());
    if (!f.open(QIODevice::ReadOnly)) return false; // missing file == no rules
    auto root = QJsonDocument::fromJson(f.readAll()).object();
    const auto hidden = root["hiddenFiles"].toObject();
    for (auto it = hidden.constBegin(); it != hidden.constEnd(); ++it) {
        QSet<QString> paths;
        for (const auto& v : it.value().toArray()) paths.insert(v.toString());
        if (!paths.isEmpty()) m_hiddenFiles.insert(it.key(), paths);
    }
    const auto overrides = root["fileOverrides"].toObject();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        const QString modId = it.value().toString();
        if (!modId.isEmpty()) m_fileOverrides.insert(it.key(), modId);
    }
    return true;
}

bool Profile::saveLoadOrderState() const {
    QJsonObject pins;
    const auto& pinned = m_pluginList.pinnedIndices();
    for (auto it = pinned.cbegin(); it != pinned.cend(); ++it)
        pins.insert(it.key(), it.value());

    QJsonObject root;
    root["loadOrderLocked"] = m_pluginList.loadOrderLocked();
    root["pinned"]          = pins;
    return atomicWrite(loadOrderStatePath(), QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool Profile::loadLoadOrderState() {
    m_pluginList.setLoadOrderLocked(false);
    m_pluginList.setPinnedIndices({});
    QFile f(loadOrderStatePath());
    if (!f.open(QIODevice::ReadOnly)) return false; // missing == unlocked, no pins
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    m_pluginList.setLoadOrderLocked(root["loadOrderLocked"].toBool(false));
    QHash<QString, int> pins;
    const auto obj = root["pinned"].toObject();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        pins.insert(it.key(), it.value().toInt());
    m_pluginList.setPinnedIndices(pins);
    return true;
}

} // namespace solero
