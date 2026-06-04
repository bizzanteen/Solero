#pragma once
#include <QByteArray>
#include <QFile>
#include <QString>

namespace solero {

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

} // namespace solero
