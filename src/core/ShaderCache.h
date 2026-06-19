#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// Result of clearShaderCache(): which ShaderCache dirs were actually removed and
// the total bytes freed (sum of file sizes deleted).
struct ShaderCacheClearResult {
    QStringList removedPaths;
    qint64 bytesRemoved = 0;
};

// Delete the Community Shaders shader cache wherever it lives, scoped strictly to
// the three ShaderCache locations:
//   - <gameDir>/Data/ShaderCache       (live, written by CS at runtime)
//   - <dataRoot>/overwrite/ShaderCache (captured into Overwrite, if any)
//   - <cacheStagingDir>/Data/ShaderCache (the managed-cache mod's staged copy)
// cacheStagingDir may be empty (no managed-cache mod) -> that location is skipped.
// Nothing outside these three ShaderCache directories is touched. Returns the
// directories removed plus the total bytes freed.
ShaderCacheClearResult clearShaderCache(const QString& gameDir,
                                        const QString& overwriteDir,
                                        const QString& cacheStagingDir);

// Capture newly-compiled shaders after a play session. Walk
// <gameDir>/Data/ShaderCache/** and, for each file whose path relative to
// <gameDir>/Data is not already present under <cacheStagingDir>/Data/, move it
// into the staging tree (creating parent dirs). Immutable shader blobs already
// present in staging are left untouched (under hardlink deploy they share an
// inode with the staged master). The one exception is Info.ini - CS's disk-cache
// validation file (per-feature enabled-state/version + plugin version) - which CS
// rewrites whenever feature state changes: it is always refreshed, so deploy stops
// restoring a stale validation file that would make CS recompile every launch.
// Returns the number of files moved. Returns 0 when cacheStagingDir is empty or
// the source ShaderCache dir is missing.
// If movedRelPaths is non-null, every successfully-moved file's path RELATIVE TO
// gameDir (e.g. "Data/ShaderCache/Effect/foo.pso") is appended to it, so the
// caller can re-link the captured files back into the live game dir.
int captureShaderCache(const QString& gameDir, const QString& cacheStagingDir,
                       QStringList* movedRelPaths = nullptr);

} // namespace solero
