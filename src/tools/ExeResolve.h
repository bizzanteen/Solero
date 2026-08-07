#pragma once
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace solero {

// Find a tool's executable in its install dir: literal path, then a
// case-insensitive shallow match, then a recursive search by name. Empty means
// it isn't there - a failed install, not a cue to guess.
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
