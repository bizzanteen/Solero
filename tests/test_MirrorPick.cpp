#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include "nexus/MirrorPick.h"
using namespace solero;

static QJsonArray arr(const QByteArray& json) {
    return QJsonDocument::fromJson(json).array();
}

class TestMirrorPick : public QObject {
    Q_OBJECT
private slots:
    void pick_emptyArray_returnsEmpty() {
        QVERIFY(pickMirror(QJsonArray{}, "Nexus CDN").isEmpty());
    }
    void pick_noPreference_returnsFirst() {
        auto a = arr("[{\"short_name\":\"Nexus CDN\",\"URI\":\"https://a/\"},"
                     "{\"short_name\":\"Amsterdam\",\"URI\":\"https://b/\"}]");
        QCOMPARE(pickMirror(a, QString()), QString("https://a/"));
    }
    void pick_preferenceMatches_returnsThatUri() {
        auto a = arr("[{\"short_name\":\"Nexus CDN\",\"URI\":\"https://a/\"},"
                     "{\"short_name\":\"Amsterdam\",\"URI\":\"https://b/\"}]");
        QCOMPARE(pickMirror(a, "Amsterdam"), QString("https://b/"));
    }
    void pick_preferenceMatchesCaseInsensitive() {
        auto a = arr("[{\"short_name\":\"Amsterdam\",\"URI\":\"https://b/\"}]");
        QCOMPARE(pickMirror(a, "amsterdam"), QString("https://b/"));
    }
    void pick_preferenceMissing_fallsBackToFirst() {
        auto a = arr("[{\"short_name\":\"Nexus CDN\",\"URI\":\"https://a/\"}]");
        QCOMPARE(pickMirror(a, "Chicago"), QString("https://a/"));
    }
    void serverNames_returnsShortNamesInOrder() {
        auto a = arr("[{\"short_name\":\"Nexus CDN\",\"URI\":\"https://a/\"},"
                     "{\"short_name\":\"Amsterdam\",\"URI\":\"https://b/\"}]");
        QStringList n = mirrorServerNames(a);
        QCOMPARE(n.size(), 2);
        QCOMPARE(n.at(0), QString("Nexus CDN"));
        QCOMPARE(n.at(1), QString("Amsterdam"));
    }
};
QTEST_MAIN(TestMirrorPick)
#include "test_MirrorPick.moc"
