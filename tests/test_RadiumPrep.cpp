#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "tools/RadiumPrep.h"
#include "core/Profile.h"
#include "core/Types.h"
using namespace solero;

class TestRadiumPrep : public QObject {
    Q_OBJECT

    static QString readAll(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(f.readAll());
    }
    static QJsonObject readJson(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    }
    static void seedPlugins(Profile& p) {
        PluginEntry a; a.filename = "Skyrim.esm";  a.enabled = true;  a.isMaster = true;
        PluginEntry b; b.filename = "SkyUI.esp";   b.enabled = true;
        PluginEntry c; c.filename = "Disabled.esp"; c.enabled = false;
        p.pluginList().append(a);
        p.pluginList().append(b);
        p.pluginList().append(c);
    }
    static void touch(const QString& path) {
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path); f.open(QIODevice::WriteOnly); f.write("x"); f.close();
    }

private slots:
    void writeFakeMo2_writesInstanceFiles() {
        QTemporaryDir tmp;
        const QString root = tmp.path();
        Profile p("ASSOS", root + "/profiles");
        seedPlugins(p);

        const QString gameDir = root + "/game";
        touch(gameDir + "/Data/ZZZ.bsa");
        touch(gameDir + "/Data/AAA.bsa");
        touch(gameDir + "/Data/textures/note.txt");

        const QString fm2 = root + "/fm2";
        QString err;
        QVERIFY2(RadiumPrep::writeFakeMo2(p, gameDir, fm2, &err), qPrintable(err));

        const QString prof = fm2 + "/profiles/solero";
        QVERIFY(QDir(fm2 + "/mods").exists());
        // enabled plugins only (Disabled.esp excluded)
        QCOMPARE(readAll(prof + "/loadorder.txt"), QString("Skyrim.esm\nSkyUI.esp\n"));
        // Data/*.bsa, sorted
        QCOMPARE(readAll(prof + "/archives.txt"), QString("AAA.bsa\nZZZ.bsa\n"));

        const QString ini = readAll(fm2 + "/ModOrganizer.ini");
        QVERIFY(ini.contains("gameName=Skyrim Special Edition"));
    }

    void writesFakeMo2AndSettings() {
        QTemporaryDir tmp;
        const QString root = tmp.path();
        Profile p("ASSOS", root + "/profiles");
        seedPlugins(p);

        const QString gameDir = root + "/game";
        touch(gameDir + "/Data/ZZZ.bsa");
        touch(gameDir + "/Data/AAA.bsa");
        touch(gameDir + "/Data/textures/note.txt");

        const QString installDir = root + "/tools/radium";
        QDir().mkpath(installDir);
        const QString outDataDir = root + "/staging/Radium Output/Data";
        const QString settings = root + "/cfg/settings.json";

        QString err;
        QVERIFY2(RadiumPrep::prepare(p, gameDir, installDir, outDataDir, settings, &err),
                 qPrintable(err));

        const QString prof = installDir + "/fake-mo2/profiles/solero";
        QVERIFY(QDir(installDir + "/fake-mo2/mods").exists());
        QVERIFY(QDir(installDir + "/fake-mo2/mods").entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
        QCOMPARE(readAll(prof + "/loadorder.txt"), QString("Skyrim.esm\nSkyUI.esp\n"));
        QCOMPARE(readAll(prof + "/archives.txt"), QString("AAA.bsa\nZZZ.bsa\n"));
        QCOMPARE(readAll(prof + "/modlist.txt"), QString(""));

        // ModOrganizer.ini at the fake-mo2 root so Radium's GUI MO2 Setup panel
        // detects the game + profile. gamePath is the Wine-style game path
        // (each '/' doubled to "\\").
        const QString ini = readAll(installDir + "/fake-mo2/ModOrganizer.ini");
        QVERIFY(ini.contains("gameName=Skyrim Special Edition"));
        QVERIFY(ini.contains("selected_profile=@ByteArray(solero)"));
        QString expectWine = gameDir; expectWine.replace('/', QStringLiteral("\\\\"));
        QVERIFY2(ini.contains("gamePath=@ByteArray(Z:" + expectWine + ")"),
                 qPrintable(ini));

        QJsonObject o = readJson(settings);
        QCOMPARE(o["manual_mode"].toBool(), true);
        QCOMPARE(o["game"].toString(), QString("SkyrimSE"));
        QCOMPARE(o["profile_path"].toString(), prof);
        QCOMPARE(o["mods_path"].toString(), installDir + "/fake-mo2/mods");
        QCOMPARE(o["data_path"].toString(), gameDir + "/Data");
        QCOMPARE(o["output_path"].toString(), outDataDir);
    }

    void preservesUserTuning() {
        QTemporaryDir tmp;
        const QString root = tmp.path();
        Profile p("ASSOS", root + "/profiles");
        seedPlugins(p);

        const QString settings = root + "/cfg/settings.json";
        QDir().mkpath(root + "/cfg");
        {
            QJsonObject pre;
            pre["preset"] = "Performance";
            pre["thread_count"] = 8;
            pre["use_gpu"] = false;
            pre["data_path"] = "/old/wrong/path";
            QFile f(settings); f.open(QIODevice::WriteOnly);
            f.write(QJsonDocument(pre).toJson()); f.close();
        }

        const QString gameDir = root + "/game";
        QDir().mkpath(gameDir + "/Data");
        const QString installDir = root + "/tools/radium";
        const QString outDataDir = root + "/staging/Radium Output/Data";

        QString err;
        QVERIFY2(RadiumPrep::prepare(p, gameDir, installDir, outDataDir, settings, &err),
                 qPrintable(err));

        QJsonObject o = readJson(settings);
        QCOMPARE(o["preset"].toString(), QString("Performance"));
        QCOMPARE(o["thread_count"].toInt(), 8);
        QCOMPARE(o["use_gpu"].toBool(), false);
        QCOMPARE(o["data_path"].toString(), gameDir + "/Data");
        QCOMPARE(o["manual_mode"].toBool(), true);
    }

    void idempotentAndEmptyData() {
        QTemporaryDir tmp;
        const QString root = tmp.path();
        Profile p("ASSOS", root + "/profiles");
        seedPlugins(p);
        const QString gameDir = root + "/game";
        QDir().mkpath(gameDir + "/Data");
        const QString installDir = root + "/tools/radium";
        const QString outDataDir = root + "/staging/Radium Output/Data";
        const QString settings = root + "/cfg/settings.json";

        QString err;
        QVERIFY(RadiumPrep::prepare(p, gameDir, installDir, outDataDir, settings, &err));
        const QString first = readAll(settings);
        QVERIFY(RadiumPrep::prepare(p, gameDir, installDir, outDataDir, settings, &err));
        QCOMPARE(readAll(settings), first);
        QCOMPARE(readAll(installDir + "/fake-mo2/profiles/solero/archives.txt"), QString(""));
    }
};

QTEST_MAIN(TestRadiumPrep)
#include "test_RadiumPrep.moc"
