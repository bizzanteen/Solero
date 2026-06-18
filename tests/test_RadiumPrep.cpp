#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "tools/RadiumPrep.h"
#include "core/Profile.h"
#include "core/ModList.h"
#include "core/StagingFolder.h"
#include "core/AppConfig.h"
#include "core/Types.h"
#include <QFileInfo>
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

    // MO2 instance population (PGPatcher path)

    static ModEntry mod(const QString& name, const QString& folder, bool enabled) {
        ModEntry e; e.type = EntryType::Mod; e.name = name; e.id = "id-" + folder;
        e.stagingFolder = folder; e.enabled = enabled; return e;
    }
    static ModEntry separator(const QString& name) {
        ModEntry e; e.type = EntryType::Separator; e.name = name; return e;
    }

    // buildMo2Modlist: lines are REVERSED vs modList index (index 0 = lowest
    // priority = BOTTOM line), +/- by enabled, separators as -<name>_separator.
    void buildMo2Modlist_orderAndPrefixes() {
        ModList ml;
        ml.append(separator("Base"));            // idx 0 -> bottom
        ml.append(mod("Low", "low_folder", true));   // idx 1
        ml.append(mod("Off", "off_folder", false));  // idx 2
        ml.append(mod("Top", "top_folder", true));   // idx 3 -> top
        QStringList lines = RadiumPrep::buildMo2Modlist(ml);
        QStringList expect{
            "+top_folder",
            "-off_folder",
            "+low_folder",
            "-" + sanitizeStagingFolder("Base") + "_separator",
        };
        QCOMPARE(lines, expect);
    }

    // Populated instance: every modlist line has a matching mods/ entry, mods
    // symlink to <staging>/<folder>/Data, separators are empty dirs, plugins.txt
    // is written MO2-style.
    void populatedInstance_symlinksAndIntegrity() {
        QTemporaryDir tmp;
        const QString root = tmp.path();
        const QString staging = root + "/staging";
        AppConfig::instance().setStagingDir(staging);

        Profile p("ASSOS", root + "/profiles");
        seedPlugins(p);

        // Two mods: one with Data, one without; plus a separator.
        p.modList().append(separator("Combat"));
        p.modList().append(mod("HasData", "has_data", true));
        p.modList().append(mod("NoData", "no_data", false));

        // Stage content for HasData only.
        touch(staging + "/has_data/Data/textures/foo.dds");

        // Pre-seed mods/ with stale entries to prove cleanup.
        const QString fm2 = root + "/fm2";
        const QString modsDir = fm2 + "/mods";
        touch(modsDir + "/stale_real_dir/keep.txt");
        QFile::link(staging + "/has_data/Data", modsDir + "/stale_link");

        const QString gameDir = root + "/game";
        QDir().mkpath(gameDir + "/Data");

        QString err;
        QVERIFY2(RadiumPrep::writeFakeMo2(p, gameDir, fm2, &err, /*populateMods=*/true),
                 qPrintable(err));

        // Stale entries gone.
        QVERIFY(!QFileInfo::exists(modsDir + "/stale_real_dir"));
        QVERIFY(!QFileInfo(modsDir + "/stale_link").isSymLink());
        // Staged data not deleted by removing the stale link.
        QVERIFY(QFileInfo::exists(staging + "/has_data/Data/textures/foo.dds"));

        // Mod with Data -> directory symlink to <staging>/<folder>/Data.
        const QFileInfo hd(modsDir + "/has_data");
        QVERIFY2(hd.isSymLink(), "has_data should be a symlink");
        QCOMPARE(hd.symLinkTarget(), QFileInfo(staging + "/has_data/Data").absoluteFilePath());
        // Mod without Data -> empty real dir.
        const QFileInfo nd(modsDir + "/no_data");
        QVERIFY(!nd.isSymLink());
        QVERIFY(nd.isDir());
        // Separator -> empty dir named <sanitized>_separator.
        const QString sepFolder = sanitizeStagingFolder("Combat") + "_separator";
        QVERIFY(QFileInfo(modsDir + "/" + sepFolder).isDir());

        // modlist.txt: top->bottom reversed; every line has a matching mods/ entry.
        const QString prof = fm2 + "/profiles/solero";
        const QStringList lines =
            readAll(prof + "/modlist.txt").split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines, (QStringList{"-no_data", "+has_data", "-" + sepFolder}));
        for (const QString& line : lines) {
            const QString folder = line.mid(1); // strip +/-
            QVERIFY2(QFileInfo::exists(modsDir + "/" + folder),
                     qPrintable("missing mods/ entry for " + line));
        }

        // plugins.txt: MO2 "*Plugin.esp" per enabled plugin (Disabled.esp excluded).
        QCOMPARE(readAll(prof + "/plugins.txt"),
                 QString("*Skyrim.esm\n*SkyUI.esp\n"));
    }
};

QTEST_MAIN(TestRadiumPrep)
#include "test_RadiumPrep.moc"
