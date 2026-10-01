#include <QtTest>
#include "tools/SteamGameLaunch.h"

using namespace solero;

class TestSteamGameLaunch : public QObject {
    Q_OBJECT
private slots:
    void appIdFromPrefix() {
        QCOMPARE(steamAppIdFromPrefix("/home/me/.local/share/Steam/steamapps/compatdata/489830"),
                 QStringLiteral("489830"));
    }
    void appIdIgnoresTrailingSlash() {
        QCOMPARE(steamAppIdFromPrefix("/steamapps/compatdata/489830/"), QStringLiteral("489830"));
    }
    void appIdRejectsNonNumericLeaf() {
        QVERIFY(steamAppIdFromPrefix("/steamapps/compatdata/pfx").isEmpty());
        QVERIFY(steamAppIdFromPrefix("/steamapps/compatdata/4898x0").isEmpty());
        QVERIFY(steamAppIdFromPrefix("").isEmpty());
    }

    void launchArgs() {
        QCOMPARE(steamAppLaunchArgs("489830"), QStringList({"-applaunch", "489830"}));
    }

    // The game takes a while to appear, so "not running yet" must not be read as
    // "session over" until it has been seen alive at least once.
    void stepWaitsBeforeFirstSighting() {
        QCOMPARE(gameSessionStep(false, false, 3000, 120000), GameSession::Starting);
    }
    void stepRunsOnFirstSighting() {
        QCOMPARE(gameSessionStep(true, false, 9000, 120000), GameSession::Running);
    }
    void stepStaysRunningWhileAlive() {
        QCOMPARE(gameSessionStep(true, true, 600000, 120000), GameSession::Running);
    }
    void stepEndsOnlyAfterSighting() {
        QCOMPARE(gameSessionStep(false, true, 600000, 120000), GameSession::Ended);
    }
    void stepGivesUpOnStartTimeout() {
        QCOMPARE(gameSessionStep(false, false, 120000, 120000), GameSession::NeverStarted);
    }
    // A game that was seen alive outlives the start timeout: the timeout must not
    // fire for a long session.
    void stepTimeoutNeverEndsLiveSession() {
        QCOMPARE(gameSessionStep(true, true, 120001, 120000), GameSession::Running);
    }
};

QTEST_MAIN(TestSteamGameLaunch)
#include "test_SteamGameLaunch.moc"
