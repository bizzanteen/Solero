#include "deploy/UnmanagedScanner.h"
#include "deploy/DeployRecord.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace solero {

namespace {

// True for a relPath that belongs to Solero's own bookkeeping in the game dir: the
// ".solero-deployed.json" record, the ".solero-backup" originals tree, and any other
// ".solero*" marker. These must never be reported as unmanaged. Checking the first path
// segment is sufficient - all of them live at the game-dir root.
bool isSoleroMeta(const QString& rel) {
    const QString top = rel.section('/', 0, 0);
    return top.startsWith(QLatin1String(".solero"), Qt::CaseInsensitive);
}

} // namespace

QSet<QString> snapshotGameFiles(const QString& gameDir) {
    QSet<QString> out;
    if (gameDir.isEmpty() || !QDir(gameDir).exists()) return out;
    const QString root = QDir(gameDir).absolutePath();
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString rel = QDir(root).relativeFilePath(it.filePath());
        if (rel.isEmpty() || rel.startsWith(QLatin1String(".."))) continue; // outside root
        if (isSoleroMeta(rel)) continue;
        out.insert(rel);
    }
    return out;
}

QStringList findUnmanagedFiles(const QString& gameDir,
                               const DeployRecord& record,
                               const QSet<QString>& baseline) {
    // Lowercased lookup sets so a case-variant on the Wine/Proton fs (Data vs data)
    // doesn't cause a managed/pre-existing file to be misreported as unmanaged.
    QSet<QString> ownedLower;
    const QStringList owned = record.allPaths();
    ownedLower.reserve(owned.size());
    for (const QString& p : owned) ownedLower.insert(p.toLower());

    QSet<QString> baseLower;
    baseLower.reserve(baseline.size());
    for (const QString& p : baseline) baseLower.insert(p.toLower());

    QStringList result;
    const QSet<QString> present = snapshotGameFiles(gameDir);
    for (const QString& rel : present) {
        const QString lo = rel.toLower();
        if (ownedLower.contains(lo)) continue; // managed by Solero
        if (baseLower.contains(lo)) continue;  // already there before the run
        result.append(rel);
    }
    std::sort(result.begin(), result.end());
    return result;
}

QStringList captureUnmanagedInto(const QString& gameDir,
                                 const DeployRecord& record,
                                 const QSet<QString>& baseline,
                                 const QString& destStagingDir) {
    const QStringList loose = findUnmanagedFiles(gameDir, record, baseline);
    const QString root = QDir(gameDir).absolutePath();
    QStringList moved;
    for (const QString& rel : loose) {
        const QString src = root + "/" + rel;
        const QString dst = destStagingDir + "/" + rel;
        QDir().mkpath(QFileInfo(dst).path());
        QFile::remove(dst); // free the slot; last capture wins if re-run
        bool ok = QFile::rename(src, dst);
        if (!ok) { // cross-fs fallback
            if (QFile::copy(src, dst)) ok = QFile::remove(src);
        }
        if (!ok) continue; // never lose a file we couldn't move; omit from result
        moved.append(rel);

        // Prune emptied parent dirs in the game dir, up to (not including) the root.
        QDir dir(QFileInfo(src).path());
        while (QDir::cleanPath(dir.path()) != QDir::cleanPath(root)
               && dir.exists() && dir.isEmpty()) {
            const QString name = dir.dirName();
            if (!dir.cdUp()) break;
            dir.rmdir(name);
        }
    }
    return moved;
}

bool saveGameSnapshot(const QString& path, const QSet<QString>& snapshot) {
    QStringList sorted(snapshot.cbegin(), snapshot.cend());
    std::sort(sorted.begin(), sorted.end());
    QJsonArray arr;
    for (const QString& p : sorted) arr.append(p);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact)) >= 0;
}

QSet<QString> loadGameSnapshot(const QString& path) {
    QSet<QString> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const auto arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto& v : arr) out.insert(v.toString());
    return out;
}

} // namespace solero
