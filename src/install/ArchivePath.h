#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// Turn an archive entry name into a safe relative path under the extraction root,
// or an empty string when the entry has to be rejected.
//
// An archive written on Windows can store "SKSE\Plugins\foo.dll" as a SINGLE entry
// name. Backslash is a legal filename character on Linux, so 7z and unrar both
// create one file called exactly that instead of the directory tree the mod
// expects. Nothing fails: the install, the deploy and the launch all report
// success, and the mod simply does nothing.
//
// Normalised away: backslashes, a leading drive letter ("C:\..."), leading
// slashes, and "." or empty components.
// Rejected (empty return): any ".." component, and anything that normalises to
// nothing. Dropping ".." rather than rejecting would silently relocate the entry
// somewhere it never asked to go, which is the zip-slip bug wearing a hat.
inline QString sanitizeArchiveEntryPath(const QString& raw) {
    QString s = raw;
    s.replace('\\', '/');
    if (s.size() >= 2 && s[1] == ':' && s[0].isLetter()) s = s.mid(2); // "C:/x" -> "/x"

    QStringList parts;
    const auto components = s.split('/');
    for (const QString& part : components) {
        if (part.isEmpty() || part == QLatin1String(".")) continue;
        if (part == QLatin1String("..")) return {};
        parts << part;
    }
    return parts.join('/');
}

} // namespace solero
