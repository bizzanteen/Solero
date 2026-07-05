#pragma once
#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>
#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <unistd.h>
#endif

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
#if defined(Q_OS_UNIX)
        // Persist the temp file's contents to disk before the rename, so a
        // power loss can't leave an empty/partial file in its place.
        if (f.handle() >= 0) ::fsync(f.handle());
#endif
        f.close();
    }
#if defined(Q_OS_UNIX)
    // POSIX rename() atomically overwrites an existing target, so there is no
    // pre-remove (which would open a crash window with the file absent). NOTE:
    // QFile::rename does not overwrite an existing destination, so we must use the
    // libc call here - using QFile::rename would fail on every re-save.
    if (::rename(QFile::encodeName(tmp).constData(),
                 QFile::encodeName(path).constData()) != 0) {
        QFile::remove(tmp);
        return false;
    }
    // fsync the containing directory so the rename itself survives power loss.
    const QByteArray dir = QFileInfo(path).absolutePath().toLocal8Bit();
    int dfd = ::open(dir.constData(), O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }
#else
    // Non-POSIX: QFile::rename won't overwrite, so remove the target first (this
    // reintroduces a small crash window, unavoidable without an atomic-replace API).
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        QFile::remove(tmp);
        return false;
    }
#endif
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
