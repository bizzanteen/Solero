#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// Archive entry name -> relative path safe to write, or empty to reject it.
// Windows-written archives can store "SKSE\Plugins\foo.dll" as one entry name,
// and backslash is a legal filename character here.
inline QString sanitizeArchiveEntryPath(const QString& raw) {
    QString s = raw;
    s.replace('\\', '/');
    if (s.size() >= 2 && s[1] == ':' && s[0].isLetter()) s = s.mid(2); // "C:/x" -> "/x"

    QStringList parts;
    const auto components = s.split('/');
    for (const QString& part : components) {
        if (part.isEmpty() || part == QLatin1String(".")) continue;
        if (part == QLatin1String("..")) return {}; // reject, don't drop: that relocates it
        parts << part;
    }
    return parts.join('/');
}

} // namespace solero
