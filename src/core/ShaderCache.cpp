#include "ShaderCache.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

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

int captureShaderCache(const QString& gameDir, const QString& cacheStagingDir) {
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
        if (QFile::exists(dstPath)) continue; // already captured - leave the live copy alone

        QDir().mkpath(QFileInfo(dstPath).path());
        if (QFile::rename(srcPath, dstPath)) {
            ++moved;
        } else if (QFile::copy(srcPath, dstPath)) {
            // Cross-filesystem rename can fail; fall back to copy + remove.
            QFile::remove(srcPath);
            ++moved;
        }
    }
    return moved;
}

} // namespace solero
