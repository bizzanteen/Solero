#include "ShaderCache.h"
#include "ModList.h"
#include "VersionUtil.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <filesystem>

namespace solero {

namespace {
// Total byte size of every file under `dir` (recursive). 0 if `dir` is missing.
qint64 dirByteSize(const QString& dir) {
    qint64 bytes = 0;
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        bytes += it.fileInfo().size();
    }
    return bytes;
}

// Remove `dir` recursively if it exists; record it + its byte size in `result`.
void removeIfPresent(const QString& dir, ShaderCacheClearResult& result) {
    QDir d(dir);
    if (!d.exists()) return;
    result.bytesRemoved += dirByteSize(dir);
    if (d.removeRecursively())
        result.removedPaths.append(QDir::cleanPath(dir));
}
} // namespace

ShaderCacheClearResult clearShaderCache(const QString& gameDir,
                                        const QString& overwriteDir,
                                        const QString& cacheStagingDir) {
    ShaderCacheClearResult result;
    removeIfPresent(gameDir + "/Data/ShaderCache", result);
    removeIfPresent(overwriteDir + "/ShaderCache", result);
    if (!cacheStagingDir.isEmpty())
        removeIfPresent(cacheStagingDir + "/Data/ShaderCache", result);
    return result;
}

int captureShaderCache(const QString& gameDir, const QString& cacheStagingDir,
                       QStringList* movedRelPaths) {
    if (cacheStagingDir.isEmpty()) return 0;

    const QString srcRoot = gameDir + "/Data/ShaderCache";
    if (!QDir(srcRoot).exists()) return 0;

    // Staging mirrors the game's Data/ tree; capture paths relative to <gameDir>/Data
    // so "ShaderCache/<rel>" lands under "<cacheStagingDir>/Data/ShaderCache/<rel>".
    const QString gameData = gameDir + "/Data";
    const QString stagingData = cacheStagingDir + "/Data";

    int moved = 0;
    // Don't follow symlinks: the runtime shader cache is never symlink-staged, so
    // following links would risk moving files from outside the tree.
    QDirIterator it(srcRoot, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString srcPath = it.next();
        const QString rel = srcPath.mid(gameData.length() + 1); // e.g. ShaderCache/foo.bin
        const QString dstPath = stagingData + "/" + rel;

        // Info.ini is CS's disk-cache VALIDATION file (per-feature enabled-state +
        // version + plugin version). CS rewrites it whenever feature state changes,
        // so a stale staged copy makes CS invalidate the cache and recompile every
        // shader on every launch. It must be refreshed, not skipped. The shader
        // blobs, by contrast, are immutable (content-hashed by descriptor) and
        // already share an inode with the staged master under hardlink deploy -
        // skip those so we don't needlessly churn them.
        const bool isValidationFile =
            QFileInfo(srcPath).fileName().compare("Info.ini", Qt::CaseInsensitive) == 0;
        if (QFile::exists(dstPath)) {
            if (!isValidationFile) continue; // already captured - leave the live copy alone
            QFile::remove(dstPath);          // refresh the stale validation file below
        }

        QDir().mkpath(QFileInfo(dstPath).path());
        bool didMove = false;
        if (QFile::rename(srcPath, dstPath)) {
            didMove = true;
        } else if (QFile::copy(srcPath, dstPath)) {
            // Cross-filesystem rename can fail; fall back to copy + remove.
            QFile::remove(srcPath);
            didMove = true;
        }
        if (didMove) {
            ++moved;
            // Path relative to gameDir (rel is relative to gameDir/Data), so the
            // caller can re-link the captured file back into the live game dir.
            if (movedRelPaths)
                movedRelPaths->append(QStringLiteral("Data/") + rel);
        }
    }
    return moved;
}

int assertShaderCacheDeployed(const QString& gameDir, const QString& cacheStagingDir,
                              bool hardlink, QStringList* relinked) {
    if (cacheStagingDir.isEmpty()) return 0;

    const QString srcRoot = cacheStagingDir + "/Data/ShaderCache";
    if (!QDir(srcRoot).exists()) return 0;

    const QString stagingData = cacheStagingDir + "/Data";
    const QString gameData = gameDir + "/Data";

    int placed = 0;
    QDirIterator it(srcRoot, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString srcPath = it.next();
        const QString rel = srcPath.mid(stagingData.length() + 1); // e.g. ShaderCache/foo.bin
        const QString dstPath = gameData + "/" + rel;

        // Leave already-correct live files alone: under hardlink deploy that means
        // the live file shares an inode with the staged master; in copy mode any
        // existing live file is assumed current (CS only ever deletes, never edits,
        // a cache file in place). Only missing or divergent files are re-placed.
        const bool present = QFile::exists(dstPath);
        if (present) {
            if (!hardlink) continue;
            std::error_code ec;
            if (std::filesystem::equivalent(srcPath.toStdString(),
                                            dstPath.toStdString(), ec) && !ec)
                continue; // same inode - already deployed
        }

        QDir().mkpath(QFileInfo(dstPath).path());
        if (present) QFile::remove(dstPath); // hardlink/copy can't overwrite

        bool done = false;
        if (hardlink) {
            std::error_code ec;
            std::filesystem::create_hard_link(srcPath.toStdString(),
                                              dstPath.toStdString(), ec);
            // Cross-device (or other) hardlink failure -> fall back to a copy.
            done = !ec || QFile::copy(srcPath, dstPath);
        } else {
            done = QFile::copy(srcPath, dstPath);
        }
        if (done) {
            ++placed;
            if (relinked)
                relinked->append(QStringLiteral("Data/") + rel);
        }
    }
    return placed;
}

QString activeCacheKey(const ModList& ml) {
    const ModEntry* cs = ml.findCommunityShaders();
    if (!cs) return QStringLiteral("default");
    if (!cs->nexusFileId.isEmpty()) return cs->nexusFileId;
    const QString nv = normalizeVersion(cs->version);
    if (!nv.isEmpty()) return nv;
    return QStringLiteral("default");
}

} // namespace solero
