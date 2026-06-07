#pragma once
#include "core/ModList.h"
#include "core/PluginList.h"
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>

namespace solero { class Profile; class ProfileManager; }

namespace solero {

// A mod named in an imported manifest that has NO match in the current mod pool.
// Recorded (and reported) rather than created as an empty placeholder.
struct MissingMod {
    QString name;
    QString nexusModId;   // empty if not from Nexus
    QString version;
};

// Result of reconstructing a parsed manifest against the current mod pool. This is
// the PURE core of import (no disk I/O), so it can be unit-tested with an in-memory
// pool. The caller turns it into a real profile + writes fomod-choices.json.
struct ManifestBuildResult {
    QString    exportedProfile;            // manifest's source profile name
    ModList    modList;                    // matched mods + separators, in manifest order
    PluginList pluginList;                 // plugin load order from the manifest
    int        modsMatched = 0;
    int        separators  = 0;
    QList<MissingMod> missing;             // listed-and-skipped (not installed here)
    // matched mod id -> the manifest's FOMOD `steps` array (to persist as a
    // fomod-choices.json under that id).
    QHash<QString, QJsonArray> fomodChoices;
};

// Result of an end-to-end manifest import (build + profile creation + back-fill).
struct ProfileImportResult {
    bool    success = false;
    QString profileName;                   // created (disambiguated) Solero profile name
    int     modsMatched = 0;
    int     separators  = 0;
    QList<MissingMod> missing;
    QString errorMessage;
};

// Portable, shareable profile manifest ("solero-profile/1"): mod list + order +
// separators + categories + per-mod FOMOD choices + plugin load order. It carries no
// mod files; on import each mod is matched against the mods installed on this machine
// (parent/child groups keyed by ordinal parentIndex, not UUID, which differs per box).
class ProfileManifest {
public:
    // Export
    // Serialise `profile` to a manifest document. fomodChoicesDir is the dir that
    // holds <modId>.json choice logs (AppConfig::dataRoot()+"/fomod-choices").
    static QJsonDocument toJson(const Profile& profile, const QString& fomodChoicesDir);
    // atomicWrite the manifest to `path`.
    static bool exportToFile(const Profile& profile, const QString& path,
                             const QString& fomodChoicesDir);

    // Import
    // Pure reconstruction: match each manifest mod against a copy of the installed
    // pool (by nexusModId+fileId, then nexusModId, then case-insensitive name; each
    // pool mod used once) and build a ModList/PluginList. Unmatched mods go in
    // `missing`, skipped rather than created as placeholders.
    static ManifestBuildResult build(const QJsonDocument& manifest, ModList pool);

    // End-to-end: parse the manifest file, build against `pool`, create a new
    // disambiguated profile (exportedProfile, " (imported)" suffix if taken),
    // save it, and back-fill fomod-choices.json for matched FOMOD mods. Does not
    // switch the active profile (the caller does).
    static ProfileImportResult importFile(const QString& manifestPath,
                                          ProfileManager& profiles,
                                          const ModList& pool,
                                          const QString& fomodChoicesDir);
};

} // namespace solero
