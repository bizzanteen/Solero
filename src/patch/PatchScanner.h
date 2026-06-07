#pragma once
#include "fomod/FomodTypes.h"
#include "fomod/FomodEngine.h"
#include <QString>
#include <QStringList>
#include <QList>
#include <functional>

namespace solero {

class Profile;

// A now-applicable-but-not-installed FOMOD patch surfaced by the wizard. It is
// either FLAG-driven (a "pick which mod you have" option whose payload lives in
// <conditionalFileInstalls>) or file-driven (a conditional install whose
// fileDependency is now satisfied by the live load order).
struct PatchCandidate {
    QString modId;
    QString modName;
    QString optionName;
    QString optionDescription;
    QString reason;            // e.g. "Folkvangr - Grass and Landscape Overhaul is installed"
    QList<FomodFile> files;    // the DELTA source->destination files to install
    QString sourceArchive;     // archive to extract those files from ("" if none)
    bool    installable = true;// false => detected only (no source archive to install from)
};

// Per-mod metadata threaded into the pure detection function.
struct PatchModMeta {
    QString modId;
    QString modName;
    QString sourceArchive;
    bool    installable = true;
};

// Identity of an enabled, installed mod - used to map a flag-setting FOMOD option
// (e.g. "Folkvangr") onto a mod the user actually has present.
struct InstalledModId {
    QString     modId;
    QString     name;             // display name
    QString     nexusName;        // nexus display name (often == name); may be empty
    QStringList pluginBasenames;  // plugins this mod provides (e.g. "Folkvangr.esp")
    QString     normalizedName;   // lowercased, punctuation-stripped name
};

using FilePresentFn      = std::function<bool(const QString&)>;
using AlreadyInstalledFn = std::function<bool(const FomodFile&)>;
// Injected wrapper over FomodEngine::collectFiles - given a selection, returns the
// files that selection installs (folding in <conditionalFileInstalls> evaluated
// against the live file-present predicate + the selection's flags).
using CollectFn          = std::function<QList<FomodFile>(const FomodEngine::Selection&)>;

// Lowercase + strip every non-alphanumeric char (for fuzzy name matching).
QString normalizeName(const QString& s);
// '\\'->'/' + lowercase (for case-insensitive path comparison).
QString normalizePath(const QString& s);

// PURE detection core (no archive/IO). Reconstructs the diff between what the
// original selection installs and what is now applicable, in two passes:
//
//   FLAG-DRIVEN: for every flag-setting option not in `original` whose step is
//     reachable and whose name maps to a present installed mod, diff
//     collect(original + {option}) against collect(original); the net-new files
//     not already on disk become a candidate ("<mod> is installed").
//
//   file-DRIVEN: any file in collect(original) that is not already installed is a
//     newly-applicable conditional/required file (its fileDependency is now
//     satisfied). These are grouped into one candidate per mod.
//
// Candidates are de-duplicated by file key and never include empty file sets.
QList<PatchCandidate> findPatches(const FomodModule& module,
                                  const FomodEngine::Selection& original,
                                  const FilePresentFn& filePresent,
                                  const AlreadyInstalledFn& alreadyInstalled,
                                  const QList<InstalledModId>& installedMods,
                                  const CollectFn& collect,
                                  const PatchModMeta& meta);

// IO wrapper: builds the load-order presence model (plugins + loose files across
// enabled mods + game Data), reconstructs each FOMOD mod's original selection from
// its fomod-choices log, and collects PatchCandidates. `progress` (if set) is
// called with each mod name as it is scanned.
QList<PatchCandidate> scanProfile(const Profile& profile,
                                  const QString& gameDir,
                                  const QString& stagingRoot,
                                  const std::function<void(const QString&)>& progress = {});

} // namespace solero
