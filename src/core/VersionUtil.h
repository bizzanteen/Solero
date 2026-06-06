#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// Strip trailing zero components from a dotted version, preserving a trailing
// non-numeric suffix on the last component. MO2's meta.ini pads versions to four
// components and tacks the file variant on the end, so the raw values are ugly:
//   "1.7.1.0"   -> "1.7.1"
//   "5.2.0.0SE" -> "5.2SE"
//   "4.3.6.0c"  -> "4.3.6c"
//   "2.0.0.0"   -> "2"
//   "11"        -> "11"     (unchanged)
inline QString normalizeVersion(const QString& in) {
    QString v = in.trimmed();
    if (v.isEmpty()) return v;
    // Split off a trailing alphabetic suffix (e.g. "SE", "c", "b").
    int cut = v.size();
    while (cut > 0 && v.at(cut - 1).isLetter()) --cut;
    const QString suffix = v.mid(cut);
    QStringList parts = v.left(cut).split('.', Qt::SkipEmptyParts);
    // Drop trailing all-zero numeric components, keeping at least one.
    while (parts.size() > 1) {
        bool allZero = true;
        for (const QChar c : parts.last()) if (c != QLatin1Char('0')) { allZero = false; break; }
        if (allZero) parts.removeLast(); else break;
    }
    if (parts.isEmpty()) return v; // no numeric part - leave as-is
    return parts.join(QLatin1Char('.')) + suffix;
}

// True only if `latest` is a strictly newer version than `installed`. Both are
// normalized first (so trailing-zero padding like "1.0.1.0" == "1.0.1" never
// flags an update). Components are compared numerically; on any non-numeric
// component mismatch (e.g. a "SE"/"c" suffix) we conservatively report not newer,
// so odd MO2 version strings don't produce false "update available" flags.
inline bool isVersionNewer(const QString& installed, const QString& latest) {
    const QString a = normalizeVersion(installed);
    const QString b = normalizeVersion(latest);
    if (a.compare(b, Qt::CaseInsensitive) == 0) return false;
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    const int n = pa.size() > pb.size() ? pa.size() : pb.size();
    for (int i = 0; i < n; ++i) {
        const QString sa = i < pa.size() ? pa.at(i) : QStringLiteral("0");
        const QString sb = i < pb.size() ? pb.at(i) : QStringLiteral("0");
        bool oka = false, okb = false;
        const int ia = sa.toInt(&oka), ib = sb.toInt(&okb);
        if (oka && okb) { if (ia != ib) return ib > ia; }
        else return false; // non-numeric component -> don't claim an update
    }
    return false;
}

} // namespace solero
