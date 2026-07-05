#pragma once
#include <QSet>
#include <QString>
#include <QStringList>

namespace solero {

class DeployRecord;

// Pure core of runtime "overwrite" capture. Solero deploys with hardlinks into the
// real game dir, so files a tool or the game writes during a run land loose; these
// helpers find them so the caller can capture them into a mod. relPaths are gameDir-
// relative and '/'-separated; comparisons are case-insensitive, .solero* excluded.

// Every regular file under `gameDir` (recursively) as gameDir-relative relPaths,
// excluding Solero metadata. Empty if `gameDir` doesn't exist. Use as the pre-run baseline.
QSet<QString> snapshotGameFiles(const QString& gameDir);

// Files present under `gameDir` now that are NEITHER owned by `record` NOR present in
// `baseline` - i.e. new, unmanaged files produced by a run. Sorted, Solero metadata
// excluded. With an empty `baseline` this returns every unmanaged file currently in the
// game dir (useful for a one-off "what's loose here?" scan), not just newly-created ones.
QStringList findUnmanagedFiles(const QString& gameDir,
                               const DeployRecord& record,
                               const QSet<QString>& baseline);

} // namespace solero
