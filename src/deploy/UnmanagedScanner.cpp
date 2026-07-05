#include "deploy/UnmanagedScanner.h"
#include "deploy/DeployRecord.h"

#include <QDir>
#include <QDirIterator>
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

} // namespace solero
