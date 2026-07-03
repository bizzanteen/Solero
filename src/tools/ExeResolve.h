#pragma once
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace solero {

// Resolve a tool's executable inside its extracted install dir. Tries the
// literal relative path first, then a case-insensitive shallow match, then a
// case-insensitive recursive search by file name (archives may nest levels).
// Returns "" when the executable genuinely isn't in the tree - callers must
// treat that as a failed install, not guess a path.
inline QString resolveToolExe(const QString& dest, const QString& exeRelPath) {
    if (exeRelPath.isEmpty()) return {};
    QString exact = dest + "/" + exeRelPath;
    if (QFile::exists(exact)) return exact;

    const QString fileName = QFileInfo(exeRelPath).fileName();
    for (const QString& e : QDir(dest).entryList(QDir::Files))
        if (e.compare(exeRelPath, Qt::CaseInsensitive) == 0) return dest + "/" + e;

    QDirIterator it(dest, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString f = it.next();
        if (QFileInfo(f).fileName().compare(fileName, Qt::CaseInsensitive) == 0) return f;
    }
    return {};
}

} // namespace solero
