#include <QtTest>
#include "nexus/NxmHandler.h"

using namespace solero;

class TestNxmHandler : public QObject { Q_OBJECT
private slots:
    void parse_validSkyrimSE() {
        auto l = NxmHandler::parse(
            "nxm://skyrimspecialedition/mods/12345/files/67890"
            "?key=abc&expires=999&user_id=1");
        QVERIFY(l.valid);
        QCOMPARE(l.game, QString("skyrimspecialedition"));
        QCOMPARE(l.modId, QString("12345"));
        QCOMPARE(l.fileId, QString("67890"));
        QCOMPARE(l.key, QString("abc"));
        QCOMPARE(l.expires, QString("999"));
    }

    void parse_validSkyrimLE() {
        auto l = NxmHandler::parse("nxm://skyrim/mods/100/files/200");
        QVERIFY(l.valid);
        QCOMPARE(l.game, QString("skyrim"));
    }

    void parse_rejectsUnknownGame() {
        // Non-Skyrim domains must be rejected so they aren't downloaded as junk.
        auto ob = NxmHandler::parse("nxm://oblivion/mods/1/files/2");
        QVERIFY(!ob.valid);
        auto fo = NxmHandler::parse("nxm://fallout4/mods/1/files/2?key=k&expires=1");
        QVERIFY(!fo.valid);
    }

    void parse_trailingSlash() {
        // A trailing slash must not break segment parsing (Qt::SkipEmptyParts).
        auto l = NxmHandler::parse("nxm://skyrimspecialedition/mods/12345/files/67890/");
        QVERIFY(l.valid);
        QCOMPARE(l.modId, QString("12345"));
        QCOMPARE(l.fileId, QString("67890"));
    }

    void parse_nonNumericIdsInvalid() {
        auto l = NxmHandler::parse("nxm://skyrimspecialedition/mods/abc/files/xyz");
        QVERIFY(!l.valid);
    }

    void isSupportedGame_cases() {
        QVERIFY(NxmHandler::isSupportedGame("skyrimspecialedition"));
        QVERIFY(NxmHandler::isSupportedGame("SkyrimSpecialEdition")); // case-insensitive
        QVERIFY(NxmHandler::isSupportedGame("skyrim"));
        QVERIFY(!NxmHandler::isSupportedGame("oblivion"));
        QVERIFY(!NxmHandler::isSupportedGame(""));
    }
};

QTEST_MAIN(TestNxmHandler)
#include "test_NxmHandler.moc"
