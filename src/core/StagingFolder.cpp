#include "StagingFolder.h"
#include "Types.h"
#include <QChar>

namespace solero {

static constexpr int kMaxBytes = 150;

// Trim a string so its UTF-8 encoding fits within `maxBytes`, never splitting a
// multibyte codepoint. Returns the longest prefix (by characters) that fits.
static QString capUtf8(const QString& s, int maxBytes) {
    if (s.toUtf8().size() <= maxBytes) return s;
    // Walk characters, accumulating UTF-8 byte length, until adding the next
    // would exceed the cap. QString iteration is by UTF-16 code unit; handle
    // surrogate pairs by encoding incrementally on the QString side.
    int chars = 0;
    int bytes = 0;
    while (chars < s.size()) {
        // Determine how many UTF-16 units the next codepoint uses.
        int units = 1;
        if (s.at(chars).isHighSurrogate() && chars + 1 < s.size()
            && s.at(chars + 1).isLowSurrogate()) {
            units = 2;
        }
        const int cpBytes = s.mid(chars, units).toUtf8().size();
        if (bytes + cpBytes > maxBytes) break;
        bytes += cpBytes;
        chars += units;
    }
    return s.left(chars);
}

static bool isReservedName(const QString& s) {
    static const QStringList reserved = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    for (const QString& r : reserved)
        if (s.compare(r, Qt::CaseInsensitive) == 0) return true;
    return false;
}

QString sanitizeStagingFolder(const QString& name) {
    static const QString illegal = QStringLiteral("/\\:*?\"<>|");
    QString out;
    out.reserve(name.size());
    bool pendingSpace = false;
    bool sawNonSpace = false;
    for (const QChar c : name) {
        QChar ch = c;
        // Whitespace (incl. tab/newline control chars) is collapsed; check it
        // before the control-char replacement so it isn't turned into '_'.
        if (ch.isSpace()) {
            // Collapse whitespace runs; defer emission so leading/trailing runs
            // can be dropped.
            if (sawNonSpace) pendingSpace = true;
            continue;
        }
        if (illegal.contains(ch) || ch.unicode() < 0x20)
            ch = QChar('_');
        if (pendingSpace) { out.append(' '); pendingSpace = false; }
        out.append(ch);
        sawNonSpace = true;
    }
    // Trim trailing dots and whitespace (alternating, since they may interleave
    // e.g. "name . . " -> "name").
    while (!out.isEmpty()
           && (out.at(out.size() - 1) == QChar('.') || out.at(out.size() - 1).isSpace()))
        out.chop(1);

    out = capUtf8(out, kMaxBytes);

    if (out.isEmpty() || isReservedName(out))
        out.prepend('_');
    return out;
}

QString uniqueStagingFolder(const QString& base, const QSet<QString>& takenLower) {
    if (!takenLower.contains(base.toLower()))
        return base;
    for (int n = 2; ; ++n) {
        const QString suffix = QStringLiteral(" (%1)").arg(n);
        QString candidate = base;
        // If base + suffix would exceed the cap, trim the base on a char
        // boundary so the whole thing fits.
        if ((candidate + suffix).toUtf8().size() > kMaxBytes) {
            candidate = capUtf8(candidate, kMaxBytes - suffix.toUtf8().size());
        }
        candidate += suffix;
        if (!takenLower.contains(candidate.toLower()))
            return candidate;
    }
}

QString stagingPathFor(const QString& stagingDir, const ModEntry& mod) {
    // Fall back to the id when no staging folder has been assigned yet (older
    // saves before migration, or a mod created before its folder was set), so
    // the resolver always points at the real on-disk location.
    const QString folder = mod.stagingFolder.isEmpty() ? mod.id : mod.stagingFolder;
    return stagingDir + "/" + folder;
}

} // namespace solero
