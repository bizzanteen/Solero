#include <QtTest>
#include <QJsonObject>
#include <QJsonDocument>
#include "tools/PgpatcherConfig.h"
using namespace solero;

class TestPgpatcherConfig : public QObject {
    Q_OBJECT
private slots:
    // winePath: native '/' separators become a SINGLE backslash in the QString
    // VALUE; QJsonDocument JSON-escapes that to "\\" on disk.
    void winePath_value() {
        QCOMPARE(PgpatcherConfig::winePath("/var/home/eamon/Modding/Tools/foo"),
                 QString("Z:\\var\\home\\eamon\\Modding\\Tools\\foo"));
        QCOMPARE(PgpatcherConfig::winePath("/home/eamon"),
                 QString("Z:\\home\\eamon"));
    }

    // The serialized JSON on disk must contain DOUBLED backslashes, matching the
    // ASSOS settings.json exactly: "Z:\\var\\home\\..." (each separator is "\\").
    void serialized_bytes_doubled_backslash() {
        QJsonObject o = PgpatcherConfig::buildSettings(
            {}, "/var/home/eamon/Game", "/home/eamon/fake-mo2", "/home/eamon/Out");
        QByteArray bytes = QJsonDocument(o).toJson(QJsonDocument::Indented);
        // C++ string "\\\\" == two backslash chars on disk.
        QVERIFY(bytes.contains("Z:\\\\var\\\\home\\\\eamon\\\\Game"));
        QVERIFY(bytes.contains("Z:\\\\home\\\\eamon\\\\fake-mo2"));
        QVERIFY(bytes.contains("Z:\\\\home\\\\eamon\\\\Out"));
        // No quadrupled backslashes (would mean we doubled twice).
        QVERIFY(!bytes.contains("\\\\\\\\\\\\"));
    }

    void sections_set_correctly() {
        QJsonObject o = PgpatcherConfig::buildSettings(
            {}, "/g/dir", "/f/mo2", "/o/dir");
        QJsonObject params = o.value("params").toObject();

        QJsonObject game = params.value("game").toObject();
        QCOMPARE(game.value("dir").toString(), QString("Z:\\g\\dir"));
        QCOMPARE(game.value("type").toInt(), 0);

        QJsonObject mm = params.value("modmanager").toObject();
        QCOMPARE(mm.value("type").toInt(), 0); // None - reads deployed Data; MO2 Set-Mods UI unusable under Proton
        QCOMPARE(mm.value("mo2instancedir").toString(), QString("Z:\\f\\mo2"));
        QCOMPARE(mm.value("mo2useloosefileorder").toBool(), true);

        QJsonObject out = params.value("output").toObject();
        QCOMPARE(out.value("dir").toString(), QString("Z:\\o\\dir"));
        QCOMPARE(out.value("pluginlang").toString(), QString("English"));
        QCOMPARE(out.value("zip").toBool(), false);
    }

    // Merge must preserve unrelated pre-existing keys (shader/patcher toggles,
    // processing, blocklists) and not clobber a user's output choices.
    void merge_preserves_existing() {
        QJsonObject existing;
        QJsonObject params;
        QJsonObject shader; shader["truepbr"] = true; shader["parallax"] = true;
        params["shaderpatcher"] = shader;
        QJsonObject proc; proc["multithread"] = false;
        params["processing"] = proc;
        QJsonObject out; out["pluginlang"] = "French"; out["zip"] = true;
        params["output"] = out;
        existing["params"] = params;

        QJsonObject o = PgpatcherConfig::buildSettings(
            existing, "/g", "/f", "/o");
        QJsonObject p = o.value("params").toObject();

        // Unrelated keys survive untouched.
        QCOMPARE(p.value("shaderpatcher").toObject().value("truepbr").toBool(), true);
        QCOMPARE(p.value("shaderpatcher").toObject().value("parallax").toBool(), true);
        QCOMPARE(p.value("processing").toObject().value("multithread").toBool(), false);

        // Solero-owned dir is updated...
        QCOMPARE(p.value("output").toObject().value("dir").toString(), QString("Z:\\o"));
        // ...but a user's existing output choices are not clobbered.
        QCOMPARE(p.value("output").toObject().value("pluginlang").toString(), QString("French"));
        QCOMPARE(p.value("output").toObject().value("zip").toBool(), true);
    }
};

QTEST_MAIN(TestPgpatcherConfig)
#include "test_PgpatcherConfig.moc"
