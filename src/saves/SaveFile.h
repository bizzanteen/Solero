#pragma once
#include <QString>
#include <QStringList>
#include <QByteArray>

// .ess header parser, read-only - it never writes, moves or deletes a save. The
// uncompressed header and screenshot are always readable. The plugin list sits
// after the screenshot, inside the compressed body on SSE saves, so it needs zlib
// or LZ4 linked in; without one, pluginsReadable is false rather than a crash.

namespace solero {

struct SaveHeader {
    bool ok = false;                 // false => malformed / truncated / not a .ess

    quint32 version = 0;             // save format version (LE ~7-9, SSE >= 12)
    quint32 saveNumber = 0;
    QString characterName;
    quint32 level = 0;
    QString location;
    QString gameDate;                // in-game time string ("Day 5 ...")
    QString raceEditorId;
    quint16 sex = 0;                 // 0 = male, 1 = female
    quint64 playtimeOrFiletime = 0;  // Windows FILETIME (8 bytes) of the save

    quint32 screenshotWidth = 0;
    quint32 screenshotHeight = 0;
    int     screenshotBytesPerPixel = 0; // 3 (RGB, LE) or 4 (RGBA, SSE)
    QByteArray screenshotRgb;        // raw pixel bytes (width*height*bpp)

    quint16 compressionType = 0;     // SSE only: 0 none, 1 zlib, 2 LZ4

    QStringList plugins;             // load order recorded in the save (may be empty)
    bool pluginsReadable = false;    // false when a compressed body couldn't be read
};

// Parse the header of the .ess at `path`. Bounds-checks every read; a truncated
// or garbage file yields {ok=false} and never crashes. Reads only - no writes.
SaveHeader parseSaveHeader(const QString& path);

// Given a parsed save and the plugin filenames present in the active load order,
// return the save's plugins that are not in that load order (compared
// case-insensitively) - MO2's "this save needs plugins you don't have" set.
// Empty when the save's plugin list is unreadable or nothing is missing.
QStringList missingPlugins(const SaveHeader& save, const QStringList& loadOrder);

// HTML summary of a save for the previewer panel. Pure presentation of
// SaveHeader; savedWhen is preformatted by the caller. An unreadable plugin list
// is noted rather than shown as a count.
QString saveSummaryHtml(const SaveHeader& save, const QString& savedWhen);

} // namespace solero
