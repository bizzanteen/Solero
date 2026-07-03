#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// Forward-declared to avoid pulling the full mod model into every ShaderCache consumer.
class ModList;

// Result of clearShaderCache(): which ShaderCache dirs were actually removed and
// the total bytes freed (sum of file sizes deleted).
struct ShaderCacheClearResult {
    QStringList removedPaths;
    qint64 bytesRemoved = 0;
};

// Delete the Community Shaders shader cache from its three locations: the live
// <gameDir>/Data/ShaderCache, <overwriteDir>/ShaderCache, and the managed-cache mod's
// <cacheStagingDir>/Data/ShaderCache (skipped when cacheStagingDir is empty). Nothing
// else is touched. Returns the dirs removed and total bytes freed.
ShaderCacheClearResult clearShaderCache(const QString& gameDir,
                                        const QString& overwriteDir,
                                        const QString& cacheStagingDir);

// Capture newly-compiled shaders after a play session: move any file under
// <gameDir>/Data/ShaderCache not already staged into <cacheStagingDir>/Data. Info.ini
// (CS's cache-validation file) is always refreshed so deploy stops restoring a stale
// copy that makes CS recompile every launch. Returns the number of files moved (0 if
// cacheStagingDir or the source dir is missing); appends moved gameDir-relative paths
// to movedRelPaths when non-null.
int captureShaderCache(const QString& gameDir, const QString& cacheStagingDir,
                       QStringList* movedRelPaths = nullptr);

// Re-assert the managed shader cache into the live game dir. Community Shaders owns
// <gameDir>/Data/ShaderCache at runtime and deletes the whole tree when it invalidates
// the cache, wiping Solero's deployed hardlinks; call this before launch (regardless of
// the deploy-clean flag) so CS validates against the staged snapshot instead of
// recompiling. For each file under <cacheStagingDir>/Data/ShaderCache, ensure a
// matching entry exists at the mirrored <gameDir>/Data path (same inode under hardlink,
// present file under copy; anything missing is re-linked/copied). Returns the count
// (re)placed and appends their gameDir-relative paths to relinked when non-null; sets
// *outFailed to the count that needed replacing but could not be linked/copied.
int assertShaderCacheDeployed(const QString& gameDir, const QString& cacheStagingDir,
                              bool hardlink, QStringList* relinked = nullptr,
                              int* outFailed = nullptr);

// Derive the cache key for the profile's Community Shaders install: the active
// CS variant's nexusFileId (most specific), then its normalised version, then
// "default". Every consumer of the managed cache derives the key through here.
QString activeCacheKey(const ModList& ml);

} // namespace solero
