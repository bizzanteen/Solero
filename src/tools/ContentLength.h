#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// Content-Length from a `curl -sIL` header dump, or -1 when it can't be
// determined (absent, chunked, unparsable). curl prints one header block per
// redirect hop, so the LAST match wins - that's the response the body comes from.
// Callers use this to turn a long download into a determinate progress bar and
// should fall back to an indeterminate one on -1.
inline qint64 contentLengthFromHeaders(const QString& headers) {
    qint64 found = -1;
    for (const QString& raw : headers.split('\n')) {
        const QString line = raw.trimmed();
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        if (line.left(colon).trimmed().compare("content-length", Qt::CaseInsensitive) != 0)
            continue;
        bool ok = false;
        const qint64 n = line.mid(colon + 1).trimmed().toLongLong(&ok);
        if (ok && n > 0) found = n;
    }
    return found;
}

} // namespace solero
