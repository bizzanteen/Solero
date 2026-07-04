#pragma once
#include <QString>
#include <QDateTime>
#include <QTimeZone>

namespace solero {

// Pure relative-time formatting for the Downloads "Downloaded" column (just now / N
// min ago / N hr ago / yesterday / N days ago / "6th Apr" past a week). Both epochs
// are Unix seconds; "now" is passed in (never read from the clock) and day boundaries
// use elapsed seconds, so the result is deterministic and timezone-independent.

// English ordinal suffix for a day-of-month (1..31): 1st, 2nd, 3rd, 4th … but
// 11th/12th/13th are all "th" (the teens exception).
inline QString ordinalSuffix(int day) {
    if (day >= 11 && day <= 13) return QStringLiteral("th");
    switch (day % 10) {
        case 1:  return QStringLiteral("st");
        case 2:  return QStringLiteral("nd");
        case 3:  return QStringLiteral("rd");
        default: return QStringLiteral("th");
    }
}

inline QString relativeDownloadTime(qint64 fileEpoch, qint64 nowEpoch) {
    const qint64 d = nowEpoch - fileEpoch;
    if (d < 60)    return QStringLiteral("just now"); // also covers future/clock-skew
    if (d < 3600)  { qint64 m = d / 60;   return QStringLiteral("%1 min%2 ago").arg(m).arg(m == 1 ? "" : "s"); }
    if (d < 86400) { qint64 h = d / 3600; return QStringLiteral("%1 hr%2 ago").arg(h).arg(h == 1 ? "" : "s"); }
    const qint64 days = d / 86400;
    if (days == 1) return QStringLiteral("yesterday");
    if (days < 7)  return QStringLiteral("%1 days ago").arg(days);
    // Oldest bucket: "6th Apr" - ordinal day (no leading zero) + abbreviated
    // month name, in UTC so the date is stable regardless of the viewer's TZ.
    const QDate date = QDateTime::fromSecsSinceEpoch(fileEpoch, QTimeZone::UTC).date();
    return QStringLiteral("%1%2 %3")
        .arg(date.day())
        .arg(ordinalSuffix(date.day()))
        .arg(date.toString(QStringLiteral("MMM")));
}

} // namespace solero
