#include "FileMove.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace solero {

int moveTreeContents(const QString& srcDir, const QString& destDir) {
    QDir src(srcDir);
    if (!src.exists()) return 0;
    QDir().mkpath(destDir);

    // Snapshot the file list (relative to srcDir) up front so moving doesn't
    // perturb the iteration.
    QStringList rels;
    {
        QDirIterator it(srcDir,
                        QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) { it.next(); rels << src.relativeFilePath(it.filePath()); }
    }

    int moved = 0;
    for (const QString& rel : rels) {
        const QString from = srcDir + "/" + rel;
        const QString to   = destDir + "/" + rel;
        QDir().mkpath(QFileInfo(to).absolutePath());
        QFile::remove(to); // rename won't overwrite an existing target on some platforms
        if (!QFile::rename(from, to)) {
            // Cross-filesystem move: rename fails across mounts, so copy + remove.
            if (QFile::copy(from, to))
                QFile::remove(from);
            else
                continue; // couldn't move this one; leave it in place
        }
        ++moved;
    }

    // Prune the now-empty source subdirectories, deepest first. rmdir only
    // removes empty directories, so any file that failed to move is preserved.
    QStringList dirs;
    {
        QDirIterator dit(srcDir, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                         QDirIterator::Subdirectories);
        while (dit.hasNext()) { dit.next(); dirs << dit.filePath(); }
    }
    std::sort(dirs.begin(), dirs.end(),
              [](const QString& a, const QString& b){ return a.length() > b.length(); });
    for (const QString& d : dirs) QDir().rmdir(d);

    return moved;
}

} // namespace solero
