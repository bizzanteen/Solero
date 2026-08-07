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

// Move newly-compiled shaders out of Data/ShaderCache into the CS mod's staging.
// Info.ini is always refreshed, or deploy keeps restoring a stale copy and CS
// recompiles every launch. Returns the count moved.
int captureShaderCache(const QString& gameDir, const QString& cacheStagingDir,
                       QStringList* movedRelPaths = nullptr);

// Put the staged cache back in the live game dir. CS owns Data/ShaderCache and
// deletes the tree when it invalidates, taking our hardlinks with it, so run this
// before every launch regardless of the deploy-clean flag. Re-links only what's
// missing; *outFailed counts what couldn't be replaced.
int assertShaderCacheDeployed(const QString& gameDir, const QString& cacheStagingDir,
                              bool hardlink, QStringList* relinked = nullptr,
                              int* outFailed = nullptr);

// The CS mod's own staging folder, which is where its shader cache lives. Empty
// when CS is absent. stagingFolder mirrors the active version variant, so the
// cache is version-scoped for free.
QString communityShadersCacheRoot(const ModList& ml, const QString& stagingDir);

} // namespace solero
