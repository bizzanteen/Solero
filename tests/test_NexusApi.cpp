#include <QtTest>
#include "nexus/NexusApi.h"
using namespace solero;

class TestNexusApi : public QObject {
    Q_OBJECT
private slots:
    // The mod-page URL is the canonical Nexus web page for a mod, used to open
    // the in-app browser at the right place. Default game = skyrimspecialedition.
    void modPageUrl_default_game() {
        QCOMPARE(NexusApi::modPageUrl("12345"),
                 QString("https://www.nexusmods.com/skyrimspecialedition/mods/12345"));
    }

    void modPageUrl_explicit_game() {
        QCOMPARE(NexusApi::modPageUrl("7", "fallout4"),
                 QString("https://www.nexusmods.com/fallout4/mods/7"));
    }
};

QTEST_MAIN(TestNexusApi)
#include "test_NexusApi.moc"
