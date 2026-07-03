#include <QtTest>
#include "ui/MenuFilter.h"

using namespace solero;

class TestMenuFilter : public QObject {
    Q_OBJECT
private slots:
    void emptyNeedleMatches() {
        QVERIFY(menuFilterMatch("", "01.4 WEAPONS"));
    }
    void substringHit() {
        QVERIFY(menuFilterMatch("weap", "01.4 WEAPONS"));
        QVERIFY(menuFilterMatch("01.4", "01.4 WEAPONS"));
    }
    void subsequenceHit() {
        // "wep" is not a substring but is an in-order subsequence of WEAPONS.
        QVERIFY(menuFilterMatch("wep", "01.4 WEAPONS"));
    }
    void caseInsensitive() {
        QVERIFY(menuFilterMatch("WEP", "01.4 weapons"));
        QVERIFY(menuFilterMatch("wEaP", "01.4 WEAPONS"));
    }
    void miss() {
        QVERIFY(!menuFilterMatch("xyz", "01.4 WEAPONS"));
    }
    void subsequenceOrderMatters() {
        // Right chars, wrong order -> not a subsequence, not a substring.
        QVERIFY(!menuFilterMatch("pew", "01.4 WEAPONS"));
    }
};

QTEST_GUILESS_MAIN(TestMenuFilter)
#include "test_MenuFilter.moc"
