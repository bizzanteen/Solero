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
        QVERIFY(winning.contains("Data/a.nif"));
        QCOMPARE(winning.size(), 2);

        auto losing = idx.losingFilesOf("mod-x");
        QVERIFY(losing.contains("Data/c.nif"));
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
