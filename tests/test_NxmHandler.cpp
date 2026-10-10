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
        // Unknown domains must be rejected so they aren't downloaded as junk.
        auto ob = NxmHandler::parse("nxm://oblivion/mods/1/files/2");
        QVERIFY(!ob.valid);
        auto remastered = NxmHandler::parse("nxm://oblivionremastered/mods/12/files/34");
        QVERIFY(remastered.valid);
        QCOMPARE(remastered.game, QString("oblivionremastered"));
        auto fo = NxmHandler::parse("nxm://fallout4/mods/1/files/2?key=k&expires=1");
        QVERIFY(!fo.valid);
    }

    void parse_trailingSlash() {
        // Nexus may include one trailing slash in links copied from a browser.
        auto l = NxmHandler::parse("nxm://skyrimspecialedition/mods/12345/files/67890/");
        QVERIFY(l.valid);
        QCOMPARE(l.modId, QString("12345"));
        QCOMPARE(l.fileId, QString("67890"));
    }

    void parse_nonNumericIdsInvalid() {
        auto l = NxmHandler::parse("nxm://skyrimspecialedition/mods/abc/files/xyz");
        QVERIFY(!l.valid);
    }

    void parse_rejectsMalformedUrls() {
        QVERIFY(!NxmHandler::parse("https://oblivionremastered/mods/1/files/2").valid);
        QVERIFY(!NxmHandler::parse("nxm://user@oblivionremastered/mods/1/files/2").valid);
        QVERIFY(!NxmHandler::parse("nxm://oblivionremastered:80/mods/1/files/2").valid);
        QVERIFY(!NxmHandler::parse("nxm://oblivionremastered/mods//1/files/2").valid);
        QVERIFY(!NxmHandler::parse("nxm://oblivionremastered/mods/1/files/2?key=only").valid);
        QVERIFY(!NxmHandler::parse("nxm://oblivionremastered/mods/1/files/2?key=a&key=b&expires=1").valid);
    }

    void parse_normalizesGameDomain() {
        const auto link = NxmHandler::parse("nxm://OblivionRemastered/mods/12/files/34");
        QVERIFY(link.valid);
        QCOMPARE(link.game, QString("oblivionremastered"));
    }

    void isSupportedGame_cases() {
        QVERIFY(NxmHandler::isSupportedGame("skyrimspecialedition"));
        QVERIFY(NxmHandler::isSupportedGame("SkyrimSpecialEdition")); // case-insensitive
        QVERIFY(NxmHandler::isSupportedGame("skyrim"));
        QVERIFY(NxmHandler::isSupportedGame("oblivionremastered"));
        QVERIFY(NxmHandler::isSupportedGame("OblivionRemastered"));
        QVERIFY(!NxmHandler::isSupportedGame("oblivion"));
        QVERIFY(!NxmHandler::isSupportedGame(""));
    }
};

QTEST_MAIN(TestNxmHandler)
#include "test_NxmHandler.moc"
