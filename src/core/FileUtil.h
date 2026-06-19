#pragma once
#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QString>

namespace solero {

// True iff `dir` exists and contains at least one regular file at any depth.
// Returns false for a missing/empty directory. Stops at the first file (cheap
// for large trees). Used to decide whether a mod's staging Data/ has output.
inline bool dirHasFiles(const QString& dir) {
    if (dir.isEmpty() || !QDir(dir).exists())
        return false;
    return QDirIterator(dir, QDir::Files, QDirIterator::Subdirectories).hasNext();
}

// Atomically write `data` to `path` via a temp file + rename.
// On any failure (open/write/rename) the original file at `path` is left
// untouched and the function returns false.
inline bool atomicWrite(const QString& path, const QByteArray& data) {
    const QString tmp = path + ".tmp";
    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        if (f.write(data) != data.size()) {
            f.close();
            QFile::remove(tmp);
            return false;
        }
        f.flush();
        f.close();
    }
    // Remove the existing target (ignore failure) so rename can succeed on
    // platforms where rename won't overwrite, then move temp into place.
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        QFile::remove(tmp);
        return false;
    }
    return true;
}

// Copy `src` over `dst`, never destroying an existing `dst` on a failed copy.
// Copies to `dst + ".tmp-solero"` first, then atomically renames over `dst`.
// Returns false (leaving any prior `dst` intact) if the copy or rename fails.
// Replaces the unsafe `QFile::remove(dst); QFile::copy(src, dst);` pattern.
inline bool copyOverwrite(const QString& src, const QString& dst) {
    const QString tmp = dst + ".tmp-solero";
    QFile::remove(tmp); // clear any leftover from a prior failed run
    if (!QFile::copy(src, tmp)) {
        QFile::remove(tmp);
        return false;
    }
    // Remove the existing target (ignore failure) so rename can overwrite on
    // platforms where it won't, then move the temp into place.
    QFile::remove(dst);
    if (!QFile::rename(tmp, dst)) {
        QFile::remove(tmp);
        return false;
    }
    return true;
}

} // namespace solero
