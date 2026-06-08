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
                                        const QString& dataRoot,
                                        const QString& cacheStagingDir);

// Capture newly-compiled shaders after a play session. Walk
// <gameDir>/Data/ShaderCache/** and, for each file whose path relative to
// <gameDir>/Data is not already present under <cacheStagingDir>/Data/, move it
// into the staging tree (creating parent dirs). Files already present in staging
// are left untouched (under hardlink deploy they share an inode with the staged
// master). Returns the number of files moved. Returns 0 when cacheStagingDir is
// empty or the source ShaderCache dir is missing.
int captureShaderCache(const QString& gameDir, const QString& cacheStagingDir);

} // namespace solero
