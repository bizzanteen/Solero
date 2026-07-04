#pragma once
#include "FomodTypes.h"
#include <QString>
#include <QStringList>
#include <QList>
#include <QSet>
#include <QHash>
#include <functional>

namespace solero {

// One archive entry + its stored CRC32 (0 = unknown). Mirrors ArchiveTool::Entry
// but kept independent so the pure reconstruction core has no install/IO deps.
struct FomodArchiveEntry { QString path; quint32 crc = 0; };

// How a FOMOD installer drives its file payloads.
//   DirectFile  - options carry their own <files>; the installed selection can be
//                 reconstructed by diffing on-disk files against the option tree.
//   FlagDriven  - options only set flags and the payloads live in
//                 <conditionalFileInstalls>; the selection is not reconstructable
//                 by file-diff (the same files install regardless of which flag).
enum class FomodClass { DirectFile, FlagDriven };

// Per-step reconstructed picks (option NAMES), matching fomod-choices.json's
// {steps:[{step, selected:[…]}]} shape.
struct ReconstructedStep { QString step; QStringList selected; };
struct ReconstructResult {
    QList<ReconstructedStep> steps;
    bool ambiguous = false; // true if any pick relied on CRC/elimination fallback
};

// pure, testable core (no IO)

// Classify a parsed module by its option/flag/conditional shape.
FomodClass classifyModule(const FomodModule& module);

// Reconstruct the installed selection by diffing the option tree against the present
// (installed) Data-relative paths, using each option file's archive destinations and
// CRC (installedCrc disambiguates same-path variants). An option is selected when its
// destinations are uniquely present in its group; SelectExactlyOne with no match
// infers the empty/"none" option by elimination.
ReconstructResult reconstructSelection(
    const FomodModule& module,
    const QList<FomodArchiveEntry>& archiveEntries,
    const QSet<QString>& installedRelPaths,
    const std::function<quint32(const QString&)>& installedCrc);

} // namespace solero
