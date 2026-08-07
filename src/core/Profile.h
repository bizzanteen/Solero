#pragma once
#include "ModList.h"
#include "PluginList.h"
#include "Types.h"
#include <QString>
#include <QList>
#include <QHash>
#include <QSet>
#include <functional>

namespace solero {

class Profile {
public:
    explicit Profile(const QString& name, const QString& rootPath);

    const QString& name() const { return m_name; }
    const QString& path() const { return m_path; }

    ModList& modList() { return m_modList; }
    const ModList& modList() const { return m_modList; }

    PluginList& pluginList() { return m_pluginList; }
    const PluginList& pluginList() const { return m_pluginList; }

    QList<Executable>& executables() { return m_executables; }
    const QList<Executable>& executables() const { return m_executables; }

    QString modlistPath()       const;
    QString pluginsPath()       const;
    QString skyrimIniPath()     const;
    QString skyrimPrefsPath()   const;
    QString skyrimCustomPath()  const;
    QString executablesPath()   const;
    QString lootUserlistPath()  const;
    QString fileRulesPath()     const;
    QString loadOrderStatePath() const;
    QString shaderCachePath()    const;
    QString profileSettingsPath() const; // small per-profile settings.json

    // per-profile local saves. When on, deploy redirects Skyrim's
    // SLocalSavePath to a per-profile "Saves/<saveFolderName()>" subfolder so each
    // profile keeps its own savegames. saveFolderName() is the filesystem/Skyrim-safe
    // form of the profile name.
    bool localSaves() const                   { return m_localSaves; }
    void setLocalSaves(bool v)                { m_localSaves = v; }
    QString saveFolderName() const            { return sanitizeSaveFolder(m_name); }
    static QString sanitizeSaveFolder(const QString& name);

    // When on, deploy/play push this profile's INIs into My Games; when off the
    // shared ones are left alone. Only INIs the profile actually has. Profiles
    // predating the flag migrate to on if they already carry INIs.
    bool localInis() const                    { return m_localInis; }
    void setLocalInis(bool v)                 { m_localInis = v; }
    bool hasProfileInis() const;              // any of the three profile INIs exist

    // Persist just settings.json (localSaves / localInis) without rewriting the whole
    // profile. Handy for flipping a flag, or setting flags on a not-yet-loaded profile.
    bool saveSettings() const;

    // Per-file conflict resolution (MO2 ".mohidden" + Vortex per-file winner).
    // relPath is in the same form DeployEngine uses: path relative to the mod
    // root (e.g. "Data/SKSE/Plugins/foo.dll").

    // A) Hidden files: a file hidden within a mod is skipped on deploy, letting
    //    the next-priority provider win.
    bool isFileHidden(const QString& modId, const QString& relPath) const;
    void setFileHidden(const QString& modId, const QString& relPath, bool hidden);
    const QHash<QString, QSet<QString>>& hiddenFiles() const { return m_hiddenFiles; }

    // B) Winner overrides: force a chosen mod to provide a path on deploy,
    //    regardless of load-order priority. Empty modId == no override.
    QString winnerOverride(const QString& relPath) const;
    void setWinnerOverride(const QString& relPath, const QString& modId);
    void clearWinnerOverride(const QString& relPath);
    const QHash<QString, QString>& fileOverrides() const { return m_fileOverrides; }

    bool save() const;
    // Persist only modlist.json (via the same atomicWrite path save() uses). A
    // pure mod-list reorder changes nothing in plugins.txt / executables.json /
    // filerules.json / shadercache.json / loadorder-state.json, so the move paths
    // use this to avoid rewriting five untouched files per drag.
    bool saveModListOnly() const;
    bool load();

    // Seed executables from a template, but only when this profile has none - it
    // never overwrites. resolveOutputMod re-resolves each tool's output mod for
    // this profile, returning empty to defer creation.
    bool seedExecutablesFrom(const QList<Executable>& templateTools,
                             const std::function<QString(const Executable&)>& resolveOutputMod);

    // Id of an existing isOutputMod mod matching `name` (case-insensitive), else
    // empty. Name-based so the seeding resolver can drive it from a template.
    static QString matchOutputModId(const ModList& modList, const QString& outputModName);

    // Resolve the on-disk staging folder name for a mod id via the active mod
    // list. Returns empty if the id is unknown or the mod has no stagingFolder
    // (older saves before migration).
    QString stagingFolderFor(const QString& id) const;

    // Backfill stagingFolder and rename UUID-named folders to sanitized mod names.
    // Idempotent. Backs up the modlist and records a reversible id->folder mapping
    // first. True if anything changed, so the caller saves.
    bool migrateStagingFolders();

private:
    QString m_name;
    QString m_path;
    ModList m_modList;
    PluginList m_pluginList;
    QList<Executable> m_executables;
    QHash<QString, QSet<QString>> m_hiddenFiles;  // modId  -> hidden relPaths
    QHash<QString, QString>       m_fileOverrides; // relPath -> forced winner modId
    bool                          m_localSaves = false;
    bool                          m_localInis  = false;     // per-profile INI files

    bool saveExecutables() const;
    bool loadExecutables();
    bool loadSettings();
    // One-time: retire the old separate managed shader-cache. Reads the legacy
    // shadercache.json (and any isManagedCache mod-list entry), moves those staging
    // folders to Trash, and deletes shadercache.json. The cache now lives inside the
    // Community Shaders mod's own staging folder. Returns true if anything changed.
    bool migrateAwayFromManagedCache();
    bool saveFileRules() const;
    bool loadFileRules();
    // Manual load-order control (lock + pins). The state lives on m_pluginList;
    // these persist it alongside the plugin list. Missing file == unlocked, no pins.
    bool saveLoadOrderState() const;
    bool loadLoadOrderState();
    // Append/update an id->folder entry in staging-folder-migration.json.
    void appendMigrationMapping(const QString& id, const QString& folder);
};

} // namespace solero
