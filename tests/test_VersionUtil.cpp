#include <QtTest>
#include "core/VersionUtil.h"

using namespace solero;

class TestVersionUtil : public QObject {
    Q_OBJECT
private slots:
    void normalize() {
        QCOMPARE(normalizeVersion("1.7.1.0"), QString("1.7.1"));
        QCOMPARE(normalizeVersion("5.2.0.0SE"), QString("5.2SE"));   // MO2 ".0" pad + variant
        QCOMPARE(normalizeVersion("4.3.6.0c"), QString("4.3.6c"));
        QCOMPARE(normalizeVersion("2.0.0.0"), QString("2"));
        QCOMPARE(normalizeVersion("1.0.1"), QString("1.0.1"));
        QCOMPARE(normalizeVersion("11"), QString("11"));
        QCOMPARE(normalizeVersion("2.2.6"), QString("2.2.6"));
        QCOMPARE(normalizeVersion("  1.2.0  "), QString("1.2"));
        QCOMPARE(normalizeVersion(""), QString(""));
    }
    void newer() {
        // trailing-zero padding is not an update
        QVERIFY(!isVersionNewer("1.0.1.0", "1.0.1"));
        QVERIFY(!isVersionNewer("5.2.0.0SE", "5.2SE"));
        // real updates
        QVERIFY(isVersionNewer("1.7.1", "1.8.0"));
        QVERIFY(isVersionNewer("1.7.1", "1.7.2"));
        QVERIFY(isVersionNewer("2", "2.0.1"));
        // older / same -> not newer
        QVERIFY(!isVersionNewer("1.8.0", "1.7.1"));
        QVERIFY(!isVersionNewer("1.0.1", "1.0.1"));
        // a non-numeric suffix mismatch is not claimed as an update (conservative)
        QVERIFY(!isVersionNewer("5.2.0.0SE", "5.2"));
    }
};

QTEST_MAIN(TestVersionUtil)
#include "test_VersionUtil.moc"
