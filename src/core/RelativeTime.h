#pragma once
#include <QString>
#include <QDateTime>
#include <QTimeZone>

namespace solero {

// Pure relative-time bucket for the Downloads "Downloaded" column.
//
// fileEpoch and nowEpoch are both seconds since the Unix epoch. "now" is passed
// in explicitly (never read from the system clock here) so the result is fully
// deterministic and unit-testable. The display buckets are:
//   < 60s              -> "just now"
//   < 60m              -> "N min ago" / "N mins ago"
//   < 24h              -> "N hr ago"  / "N hrs ago"
//   1 day              -> "yesterday"
//   2..6 days          -> "N days ago"
//   >= 7 days          -> "dd/MM" (UTC, so the date is stable regardless of TZ)
//
// Day boundaries use elapsed seconds (not calendar dates) so the function stays
// pure and timezone-independent for every relative bucket.
inline QString relativeDownloadTime(qint64 fileEpoch, qint64 nowEpoch) {
    const qint64 d = nowEpoch - fileEpoch;
    if (d < 60)    return QStringLiteral("just now"); // also covers future/clock-skew
    if (d < 3600)  { qint64 m = d / 60;   return QStringLiteral("%1 min%2 ago").arg(m).arg(m == 1 ? "" : "s"); }
    if (d < 86400) { qint64 h = d / 3600; return QStringLiteral("%1 hr%2 ago").arg(h).arg(h == 1 ? "" : "s"); }
    const qint64 days = d / 86400;
    if (days == 1) return QStringLiteral("yesterday");
    if (days < 7)  return QStringLiteral("%1 days ago").arg(days);
    return QDateTime::fromSecsSinceEpoch(fileEpoch, QTimeZone::UTC).toString(QStringLiteral("dd/MM"));
}

} // namespace solero
