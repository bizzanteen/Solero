#include <QtTest>
#include "core/RelativeTime.h"

using namespace solero;

// A fixed reference "now" so no ambient system time ever leaks into the tests.
// 2026-06-15 12:00:00 UTC.
static constexpr qint64 kNow = 1781524800LL;

class TestRelativeTime : public QObject {
    Q_OBJECT
private slots:
    void justNow() {
        QCOMPARE(relativeDownloadTime(kNow,       kNow), QStringLiteral("just now"));
        QCOMPARE(relativeDownloadTime(kNow - 1,   kNow), QStringLiteral("just now"));
        QCOMPARE(relativeDownloadTime(kNow - 59,  kNow), QStringLiteral("just now"));
    }
    void futureClampsToJustNow() {
        QCOMPARE(relativeDownloadTime(kNow + 500, kNow), QStringLiteral("just now"));
    }
    void minutes() {
        QCOMPARE(relativeDownloadTime(kNow - 60,   kNow), QStringLiteral("1 min ago"));
        QCOMPARE(relativeDownloadTime(kNow - 120,  kNow), QStringLiteral("2 mins ago"));
        QCOMPARE(relativeDownloadTime(kNow - 3599, kNow), QStringLiteral("59 mins ago"));
    }
    void hours() {
        QCOMPARE(relativeDownloadTime(kNow - 3600,  kNow), QStringLiteral("1 hr ago"));
        QCOMPARE(relativeDownloadTime(kNow - 7200,  kNow), QStringLiteral("2 hrs ago"));
        QCOMPARE(relativeDownloadTime(kNow - 86399, kNow), QStringLiteral("23 hrs ago"));
    }
    void yesterday() {
        QCOMPARE(relativeDownloadTime(kNow - 86400,  kNow), QStringLiteral("yesterday"));
        QCOMPARE(relativeDownloadTime(kNow - 172799, kNow), QStringLiteral("yesterday"));
    }
    void days() {
        QCOMPARE(relativeDownloadTime(kNow - 172800, kNow), QStringLiteral("2 days ago"));
        QCOMPARE(relativeDownloadTime(kNow - 6 * 86400, kNow), QStringLiteral("6 days ago"));
    }
    void olderShowsDate() {
        // 2026-06-15 minus 10 days = 2026-06-05 -> "05/06" in UTC.
        QCOMPARE(relativeDownloadTime(kNow - 10 * 86400, kNow), QStringLiteral("05/06"));
        // Exactly 7 days rolls over to the date bucket.
        QCOMPARE(relativeDownloadTime(kNow - 7 * 86400, kNow), QStringLiteral("08/06"));
    }
};

QTEST_GUILESS_MAIN(TestRelativeTime)
#include "test_RelativeTime.moc"
