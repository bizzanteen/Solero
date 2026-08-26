// Smoothed time-remaining estimate for the Wabbajack download/install phases.
#include <QtTest>
#include "wabbajack/DownloadEta.h"
using namespace solero;

static constexpr double MB = 1024.0 * 1024.0;

class TestDownloadEta : public QObject {
    Q_OBJECT
private slots:
    void saysNothingUntilTheRateSettles() {
        EtaEstimator e;
        QCOMPARE(e.secondsRemaining(), -1.0);
        e.sample(100 * MB, 0);
        QCOMPARE(e.secondsRemaining(), -1.0);   // baseline only
        e.sample(90 * MB, 2000);
        e.sample(80 * MB, 4000);
        QCOMPARE(e.secondsRemaining(), -1.0);   // two readings is still too few
        e.sample(70 * MB, 6000);
        QVERIFY(e.secondsRemaining() > 0);
    }

    // 10MB every 2s = 5MB/s, so 70MB left is 14s.
    void estimatesFromASteadyRate() {
        EtaEstimator e;
        for (int i = 0; i <= 3; ++i) e.sample((100 - 10 * i) * MB, i * 2000);
        QVERIFY(qAbs(e.bytesPerSecond() - 5 * MB) < 1024);
        QVERIFY(qAbs(e.secondsRemaining() - 14.0) < 0.1);
    }

    void ignoresReadingsTakenTooCloseTogether() {
        EtaEstimator e;
        e.sample(100 * MB, 0);
        e.sample(99 * MB, 100);
        e.sample(98 * MB, 200);
        e.sample(97 * MB, 300);
        QCOMPARE(e.secondsRemaining(), -1.0);
    }

    // A real stall shouldn't leave a stale ETA standing.
    void aLongStallDragsTheRateDown() {
        EtaEstimator e;
        for (int i = 0; i <= 3; ++i) e.sample((100 - 10 * i) * MB, i * 2000);
        const double before = e.secondsRemaining();
        for (int i = 1; i <= 5; ++i) e.sample(70 * MB, 6000 + i * 35000);
        QVERIFY(e.secondsRemaining() > before);
    }

    // The engine rounds "remaining" to 0.1GB, so at GB scale the same figure is
    // reprinted for ten-odd seconds at a time. Those repeats are not a stall - the
    // rate has to come out of the gap between two figures that actually differ.
    void survivesTheEnginesRoundedRemainingFigure() {
        const double GB = 1024.0 * MB;
        EtaEstimator e;
        // 61.4GB at a true 10MB/s: one 0.1GB step roughly every 10s, printed every 2s.
        double rem = 61.4;
        qint64 t = 0;
        for (int step = 0; step < 6; ++step) {
            for (int i = 0; i < 5; ++i, t += 2000) e.sample(rem * GB, t);
            rem -= 0.1;
        }
        const double rate = e.bytesPerSecond();
        QVERIFY2(rate > 8 * MB && rate < 12 * MB,
                 qPrintable(QString::number(rate / MB) + "MB/s"));
    }

    // The engine discovers more archives mid-run, so remaining can go up. That is
    // not negative throughput - keep the rate and re-baseline.
    void treatsAGrowingRemainderAsNewWorkNotNegativeSpeed() {
        EtaEstimator e;
        for (int i = 0; i <= 3; ++i) e.sample((100 - 10 * i) * MB, i * 2000);
        const double rate = e.bytesPerSecond();
        e.sample(200 * MB, 8000);
        QCOMPARE(e.bytesPerSecond(), rate);
        QVERIFY(e.secondsRemaining() > 0);
    }

    void rejectsUnknownRemainders() {
        EtaEstimator e;
        for (int i = 0; i <= 3; ++i) e.sample((100 - 10 * i) * MB, i * 2000);
        const double before = e.secondsRemaining();
        e.sample(-1, 10000);
        QCOMPARE(e.secondsRemaining(), before);
    }

    void resetClearsTheHistory() {
        EtaEstimator e;
        for (int i = 0; i <= 3; ++i) e.sample((100 - 10 * i) * MB, i * 2000);
        QVERIFY(e.secondsRemaining() > 0);
        e.reset();
        QCOMPARE(e.secondsRemaining(), -1.0);
        QCOMPARE(e.bytesPerSecond(), -1.0);
    }

    void formatsDurations() {
        QCOMPARE(formatDuration(0), QStringLiteral("0s"));
        QCOMPARE(formatDuration(45), QStringLiteral("45s"));
        QCOMPARE(formatDuration(59.6), QStringLiteral("1m"));
        QCOMPARE(formatDuration(60), QStringLiteral("1m"));
        QCOMPARE(formatDuration(380), QStringLiteral("6m 20s"));
        QCOMPARE(formatDuration(3600), QStringLiteral("1h"));
        QCOMPARE(formatDuration(7500), QStringLiteral("2h 5m"));
        QCOMPARE(formatDuration(90000), QStringLiteral("1d 1h"));
        QCOMPARE(formatDuration(86400), QStringLiteral("1d"));
        QCOMPARE(formatDuration(-1), QString());
    }
};

QTEST_MAIN(TestDownloadEta)
#include "test_DownloadEta.moc"
