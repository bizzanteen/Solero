#pragma once
#include "fomod/FomodTypes.h"
#include "fomod/FomodEngine.h"
#include <QString>
#include <QStringList>
#include <QList>
#include <functional>

namespace solero {

class Profile;

// A now-applicable-but-not-installed FOMOD patch surfaced by the wizard: a
// conditional/optional FOMOD file whose fileDependency is now satisfied by the
// live load order but which is not already on disk. Every candidate names a
// concrete present trigger (a plugin basename or loose file).
struct PatchCandidate {
    QString modId;
    QString modName;
    QString optionName;
    QString optionDescription;
    QString reason;            // e.g. "Requires SkyUI_SE.esp (present)"
    QList<FomodFile> files;    // the DELTA source->destination files to install
    QString sourceArchive;     // archive to extract those files from ("" if none)
    bool    installable = true;// false => detected only (no source archive to install from)
    QString stagingDir;        // absolute path to the owning mod's staging folder
};

// Per-mod metadata threaded into the pure detection function.
struct PatchModMeta {
    QString modId;
    QString modName;
    QString sourceArchive;
    bool    installable = true;
    QString stagingDir;
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

// Pure detection core (no archive/IO): surfaces conditional/optional FOMOD files
// whose fileDependency the live load order now satisfies and that are not already
// installed - either an option's own <files> that became Required/Recommended, or
// files from <conditionalFileInstalls> (grouped one candidate per mod). Only emits a
// candidate when a concrete present trigger can be named; de-duplicated by file key.
QList<PatchCandidate> findPatches(const FomodModule& module,
                                  const FomodEngine::Selection& original,
                                  const FilePresentFn& filePresent,
                                  const AlreadyInstalledFn& alreadyInstalled,
                                  const CollectFn& collect,
                                  const PatchModMeta& meta);

// Establish a FOMOD mod's original install selection. When a fomod-choices log
// exists at `choicesLogPath` (Solero-installed mods), it is used verbatim and
// `reconstruct` is not invoked (`reconstructed` set false). Otherwise (imported
// MO2/Wabbajack mods) `reconstruct` supplies the selection via file-diff and
// `reconstructed` is set true.
FomodEngine::Selection establishSelection(
    const FomodModule& module,
    const QString& choicesLogPath,
    const std::function<FomodEngine::Selection()>& reconstruct,
    bool& reconstructed);

// IO wrapper: builds the load-order presence model (plugins + loose files across
// enabled mods + game Data), reconstructs each FOMOD mod's original selection from
// its fomod-choices log, and collects PatchCandidates. `progress` (if set) is
// called with each mod name as it is scanned.
QList<PatchCandidate> scanProfile(const Profile& profile,
                                  const QString& gameDir,
                                  const QString& stagingRoot,
                                  const std::function<void(const QString&)>& progress = {});

} // namespace solero
