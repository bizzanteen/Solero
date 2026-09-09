// When a tool run has to disable its own output mod first.
#include <QtTest>
#include "tools/OutputModGate.h"
using namespace solero;

class TestOutputModGate : public QObject {
    Q_OBJECT
private slots:
    void clearsWhenAPatcherWouldReadItsOwnOutput() {
        QVERIFY(shouldClearOutputBeforeRun(true, true, true, true));
    }

    // Two deploys either side of the run is a real cost; don't pay it for tools
    // that don't re-ingest.
    void leavesToolsThatDontAskAlone() {
        QVERIFY(!shouldClearOutputBeforeRun(false, true, true, true));
    }

    // First run: nothing staged, so nothing deployed to get in the way.
    void skipsTheFirstRun() {
        QVERIFY(!shouldClearOutputBeforeRun(true, true, true, false));
    }

    // Already off - no disable to do, and no reason for the extra deploys.
    void skipsAnOutputModTheUserAlreadyDisabled() {
        QVERIFY(!shouldClearOutputBeforeRun(true, true, false, true));
    }

    void skipsWhenNoOutputModIsWired() {
        QVERIFY(!shouldClearOutputBeforeRun(true, false, true, true));
        QVERIFY(!shouldClearOutputBeforeRun(true, false, false, false));
    }
};

QTEST_MAIN(TestOutputModGate)
#include "test_OutputModGate.moc"
