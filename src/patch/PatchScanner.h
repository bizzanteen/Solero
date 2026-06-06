#pragma once
#include "fomod/FomodTypes.h"
#include <QString>
#include <QList>
#include <functional>

namespace solero {

class Profile;

// A FOMOD optional option that is now applicable to the current load order
// (its required plugin/file is present) but was not installed into the mod.
struct PatchCandidate {
    QString modId;
    QString modName;
    QString optionName;
    QString optionDescription;
    QString reason;            // e.g. "Requires SkyUI_SE.esp (present)"
    QList<FomodFile> files;    // the option's source->destination files to install
    QString sourceArchive;     // archive to extract those files from
};

// Per-mod metadata threaded into the pure detection function.
struct PatchModMeta {
    QString modId;
    QString modName;
    QString sourceArchive;
};

using FilePresentFn      = std::function<bool(const QString&)>;
using AlreadyInstalledFn = std::function<bool(const FomodFile&)>;

// PURE detection logic (no archive/IO): given a parsed FomodModule and injected
// predicates, returns the options that are now-applicable-but-not-installed.
//
// An option is a candidate iff all of:
//   (a) it has files to install, and
//   (b) its effective type is not NotUsable, and
//   (c) it (or its step's <visible> gate, or its <dependencyType>) references a
//       fileDependency on a file that filePresent() now returns TRUE for, and
//   (d) at least one of its destination files is not already installed.
// Options with no fileDependency at all are skipped (they are not conditional
// "patches").
QList<PatchCandidate> candidatesForModule(const FomodModule& module,
                                          const FilePresentFn& filePresent,
                                          const AlreadyInstalledFn& alreadyInstalled,
                                          const PatchModMeta& meta);

// IO wrapper: walks the profile's enabled FOMOD mods, re-parses each mod's
// ModuleConfig from its source archive, and collects PatchCandidates. `progress`
// (if set) is called with each mod name as it is scanned.
QList<PatchCandidate> scanProfile(const Profile& profile,
                                  const QString& gameDir,
                                  const QString& stagingRoot,
                                  const std::function<void(const QString&)>& progress = {});

} // namespace solero
