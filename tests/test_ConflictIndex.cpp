#include <QtTest>
#include <QTemporaryDir>
#include "deploy/ConflictIndex.h"
using namespace solero;

class TestConflictIndex : public QObject {
    Q_OBJECT
private slots:
    void noConflict_returnsEmpty() {
        ConflictIndex idx;
        idx.setWinner("Data/foo.nif", "mod-a");
        QCOMPARE(idx.losersOf("Data/foo.nif").size(), 0);
        QCOMPARE(idx.winnerOf("Data/foo.nif"), QString("mod-a"));
    }
    void conflict_tracksWinnerAndLosers() {
        ConflictIndex idx;
        idx.setWinner("Data/foo.nif", "mod-a");
        idx.recordConflict("Data/foo.nif", "mod-a", "mod-b");
        idx.recordConflict("Data/foo.nif", "mod-a", "mod-c");
        QCOMPARE(idx.winnerOf("Data/foo.nif"), QString("mod-a"));
        auto losers = idx.losersOf("Data/foo.nif");
        QVERIFY(losers.contains("mod-b"));
        QVERIFY(losers.contains("mod-c"));
    }
    void winningFiles_returnsFilesWonByMod() {
        ConflictIndex idx;
        idx.setWinner("Data/a.nif", "mod-x");
        idx.recordConflict("Data/a.nif", "mod-x", "mod-y");
        idx.setWinner("Data/b.nif", "mod-x");
        idx.setWinner("Data/c.nif", "mod-y");
        idx.recordConflict("Data/c.nif", "mod-y", "mod-x");

        auto winning = idx.winningFilesOf("mod-x");
        QVERIFY(winning.contains("Data/a.nif"));  // a.nif has a loser (mod-y)
        QVERIFY(!winning.contains("Data/b.nif")); // b.nif has no losers - not a conflict win
        QCOMPARE(winning.size(), 1);

        auto losing = idx.losingFilesOf("mod-x");
        QVERIFY(losing.contains("Data/c.nif"));
    }
    void reverseIndex_invalidatesAfterQuery() {
        // winningFilesOf/losingFilesOf are backed by a lazily-built reverse
        // index. Querying it (building the cache) and then mutating must not return
        // stale results - the mutation has to invalidate the cache.
        ConflictIndex idx;
        idx.setWinner("Data/a.nif", "mod-x");
        idx.recordConflict("Data/a.nif", "mod-x", "mod-y");
        QCOMPARE(idx.winningFilesOf("mod-x").size(), 1); // builds the reverse cache

        // Add a second conflict after the cache was built.
        idx.setWinner("Data/b.nif", "mod-x");
        idx.recordConflict("Data/b.nif", "mod-x", "mod-z");
        auto winning = idx.winningFilesOf("mod-x");
        QCOMPARE(winning.size(), 2);
        QVERIFY(winning.contains("Data/a.nif"));
        QVERIFY(winning.contains("Data/b.nif"));
        QVERIFY(idx.losingFilesOf("mod-z").contains("Data/b.nif"));

        idx.clear();
        QCOMPARE(idx.winningFilesOf("mod-x").size(), 0);
    }
    void roundtripJson() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/conflicts.json";
        ConflictIndex idx;
        idx.setWinner("Data/foo.esp", "mod-1");
        idx.recordConflict("Data/foo.esp", "mod-1", "mod-2");
        idx.saveToFile(path);

        ConflictIndex loaded = ConflictIndex::loadFromFile(path);
        QCOMPARE(loaded.winnerOf("Data/foo.esp"), QString("mod-1"));
        QVERIFY(loaded.losersOf("Data/foo.esp").contains("mod-2"));
    }
};
QTEST_MAIN(TestConflictIndex)
#include "test_ConflictIndex.moc"
