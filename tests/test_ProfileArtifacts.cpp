#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "core/Profile.h"
#include "core/PluginList.h"
#include "core/AppConfig.h"
#include "deploy/ProfileArtifacts.h"
#include "bethini/IniFile.h"
using namespace solero;

// The generated artifacts (Plugins.txt / loadorder.txt / INIs) are what the game
// actually reads. They used to be written only inside DeployEngine, so any path
// that skipped a deploy - notably launching with an already-deployed, clean modlist
// - left the game loading the load order from the LAST deploy. These tests pin the
// writer down on its own, away from the deploy engine, so both callers can use it.
class TestProfileArtifacts : public QObject {
    Q_OBJECT

    struct Env {
        QTemporaryDir tmp;
        QString game, appdata, docs, profiles;
    };
    static void setup(Env& e) {
        e.game     = e.tmp.path() + "/game";     QDir().mkpath(e.game);
        e.appdata  = e.tmp.path() + "/appdata";  QDir().mkpath(e.appdata);
        e.docs     = e.tmp.path() + "/docs";     QDir().mkpath(e.docs);
        e.profiles = e.tmp.path() + "/profiles";
        AppConfig::instance().setLocalAppDataDir(e.appdata);
        AppConfig::instance().setDocumentsDir(e.docs);
    }
    static void writeFile(const QString& p, const QByteArray& c) {
        QDir().mkpath(QFileInfo(p).path());
        QFile f(p); QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(p)); f.write(c);
    }
    static PluginEntry plugin(const QString& name, bool enabled) {
        PluginEntry p;
        p.filename = name;
        p.enabled  = enabled;
        p.isMaster = name.endsWith(".esm", Qt::CaseInsensitive);
        p.isLight  = name.endsWith(".esl", Qt::CaseInsensitive);
        return p;
    }
    static QString readAll(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        return QString::fromUtf8(f.readAll());
    }

private slots:
    // The bug: a reordered load order must reach the game's Plugins.txt without a
    // deploy. Writing the artifacts is the whole job, so this is the regression
    // test for "changed the order, hit Play, game loaded the old order".
    void writesEnabledOrderToLocalAppData() {
        Env e; setup(e);
        Profile p("T", e.profiles);
        p.pluginList().append(plugin("Skyrim.esm", true));
        p.pluginList().append(plugin("KiNRefined.esp", true));
        p.pluginList().append(plugin("RSChildren.esp", true));
        p.pluginList().append(plugin("Disabled.esp", false));

        const auto first = writeProfileArtifacts(p, e.game);
        QVERIFY2(first.ok, qPrintable(first.errors.join("; ")));
        QCOMPARE(readAll(e.appdata + "/Plugins.txt"),
                 QString("*Skyrim.esm\n*KiNRefined.esp\n*RSChildren.esp\nDisabled.esp\n"));

        // Swap the two esps, as a drag in the plugin list would, and write again.
        auto& pl = p.pluginList();
        pl.move(1, 2);
        const auto second = writeProfileArtifacts(p, e.game);
        QVERIFY2(second.ok, qPrintable(second.errors.join("; ")));
        QCOMPARE(readAll(e.appdata + "/Plugins.txt"),
                 QString("*Skyrim.esm\n*RSChildren.esp\n*KiNRefined.esp\nDisabled.esp\n"));
    }

    // loadorder.txt carries the FULL order (disabled plugins included, no '*') and
    // must be rewritten alongside Plugins.txt, or tools that read it disagree with
    // the game about the ordering.
    void writesFullOrderToLoadOrderTxt() {
        Env e; setup(e);
        Profile p("T", e.profiles);
        p.pluginList().append(plugin("Skyrim.esm", true));
        p.pluginList().append(plugin("Off.esp", false));
        p.pluginList().append(plugin("On.esp", true));

        QVERIFY(writeProfileArtifacts(p, e.game).ok);
        QCOMPARE(readAll(e.appdata + "/loadorder.txt"),
                 QString("Skyrim.esm\nOff.esp\nOn.esp\n"));
    }

    // Every artifact it wrote is reported back, so DeployEngine can record the ones
    // that land inside the game dir and undeploy can clean them up.
    void reportsWrittenPaths() {
        Env e; setup(e);
        Profile p("T", e.profiles);
        p.pluginList().append(plugin("Skyrim.esm", true));

        const auto r = writeProfileArtifacts(p, e.game);
        QVERIFY(r.written.contains(e.appdata + "/Plugins.txt"));
        QVERIFY(r.written.contains(e.appdata + "/loadorder.txt"));
    }

    // With no prefix configured, the load order falls back into Data/ rather than
    // going nowhere (MainWindow warns separately that the game won't read it there).
    void fallsBackToGameDataWhenPrefixUnknown() {
        Env e; setup(e);
        AppConfig::instance().setLocalAppDataDir({});
        Profile p("T", e.profiles);
        p.pluginList().append(plugin("Skyrim.esm", true));

        QVERIFY(writeProfileArtifacts(p, e.game).ok);
        QCOMPARE(loadOrderDir(e.game), e.game + "/Data");
        QVERIFY(QFileInfo::exists(e.game + "/Data/Plugins.txt"));
    }

    // A profile whose plugin list hasn't been populated must never blank out a live
    // load order - an empty Plugins.txt makes the game load no mods at all. Launch
    // writes the artifacts on every start, so this has to be safe.
    void neverBlanksLiveOrderWithAnEmptyPluginList() {
        Env e; setup(e);
        writeFile(e.appdata + "/Plugins.txt", "*Skyrim.esm\n*Real.esp\n");
        writeFile(e.appdata + "/loadorder.txt", "Skyrim.esm\nReal.esp\n");
        Profile p("T", e.profiles);
        QCOMPARE(p.pluginList().count(), 0);

        const auto r = writeProfileArtifacts(p, e.game);
        QVERIFY2(r.ok, qPrintable(r.errors.join("; ")));
        QCOMPARE(readAll(e.appdata + "/Plugins.txt"), QString("*Skyrim.esm\n*Real.esp\n"));
        QCOMPARE(readAll(e.appdata + "/loadorder.txt"), QString("Skyrim.esm\nReal.esp\n"));
        QVERIFY(!r.written.contains(e.appdata + "/Plugins.txt"));
    }

    // ...but an empty plugin list with no live order yet (a fresh install) still
    // writes, so the file exists for the game and for LOOT.
    void emptyPluginListStillWritesWhenNoLiveOrder() {
        Env e; setup(e);
        Profile p("T", e.profiles);

        const auto r = writeProfileArtifacts(p, e.game);
        QVERIFY(r.ok);
        QVERIFY(r.written.contains(e.appdata + "/Plugins.txt"));
    }

    // The guard keys on real plugin entries, not on the file being non-empty: a
    // plugin-less profile writes a one-newline Plugins.txt, and writing that again
    // on the next deploy must not count as clobbering anything.
    void emptyPluginListRewritesItsOwnEmptyFile() {
        Env e; setup(e);
        Profile p("T", e.profiles);

        QVERIFY(writeProfileArtifacts(p, e.game).ok);
        const auto again = writeProfileArtifacts(p, e.game);
        QVERIFY(again.written.contains(e.appdata + "/Plugins.txt"));
    }

    // A preserved artifact is still reported as being in place. DeployEngine records
    // what this call leaves behind, and an unreported file looks stale to the deploy
    // sweep - which would delete the very load order the guard just protected.
    void reportsPreservedArtifactsSoTheyAreNotSweptAway() {
        Env e; setup(e);
        AppConfig::instance().setLocalAppDataDir({}); // artifacts land inside gameDir
        writeFile(e.game + "/Data/Plugins.txt", "*Skyrim.esm\n*Real.esp\n");
        Profile p("T", e.profiles);

        const auto r = writeProfileArtifacts(p, e.game);
        QVERIFY(r.written.isEmpty() || !r.written.contains(e.game + "/Data/Plugins.txt"));
        QVERIFY(r.inPlace.contains(e.game + "/Data/Plugins.txt"));
    }

    // localInis ON: the profile's own INIs are pushed to My Games on every write,
    // so a BethINI change reaches the game without a deploy.
    void copiesProfileInisWhenLocal() {
        Env e; setup(e);
        Profile p("T", e.profiles);
        p.setLocalInis(true);
        writeFile(p.skyrimPrefsPath(), "[Display]\niSize W=1920\n");

        const auto r = writeProfileArtifacts(p, e.game);
        QVERIFY(r.ok);
        QCOMPARE(readAll(e.docs + "/SkyrimPrefs.ini"), QString("[Display]\niSize W=1920\n"));
        QVERIFY(r.written.contains(e.docs + "/SkyrimPrefs.ini"));
    }

    // localInis OFF: the live/shared INIs are left exactly as they are.
    void leavesLiveInisAloneWhenNotLocal() {
        Env e; setup(e);
        Profile p("T", e.profiles);
        p.setLocalInis(false);
        writeFile(p.skyrimPrefsPath(), "[Display]\niSize W=1920\n");
        writeFile(e.docs + "/SkyrimPrefs.ini", "[Display]\niSize W=2560\n");

        QVERIFY(writeProfileArtifacts(p, e.game).ok);
        QCOMPARE(readAll(e.docs + "/SkyrimPrefs.ini"), QString("[Display]\niSize W=2560\n"));
    }

    // localSaves toggles the SLocalSavePath redirect in the live Skyrim.ini, both on
    // and off, and creates the per-profile folder.
    void togglesSaveRedirect() {
        Env e; setup(e);
        Profile p("MyChar", e.profiles);

        p.setLocalSaves(true);
        QVERIFY(writeProfileArtifacts(p, e.game).ok);
        IniFile on; on.load(e.docs + "/Skyrim.ini");
        QCOMPARE(on.value("General", "SLocalSavePath"), QString("Saves\\MyChar\\"));
        QVERIFY(QFileInfo::exists(e.docs + "/Saves/MyChar"));

        p.setLocalSaves(false);
        QVERIFY(writeProfileArtifacts(p, e.game).ok);
        IniFile off; off.load(e.docs + "/Skyrim.ini");
        QVERIFY(!off.has("General", "SLocalSavePath"));
    }

    // The launch-time safety net: silence once the artifacts are written, and a
    // report when the file the game reads doesn't match the profile after all.
    void mismatchDetectorSpotsStaleLiveOrder() {
        Env e; setup(e);
        Profile p("T", e.profiles);
        p.pluginList().append(plugin("Skyrim.esm", true));
        p.pluginList().append(plugin("A.esp", true));
        p.pluginList().append(plugin("B.esp", true));

        // Nothing written yet - the game would load its own stale order.
        QVERIFY(!liveLoadOrderMismatch(p, e.game).isEmpty());

        QVERIFY(writeProfileArtifacts(p, e.game).ok);
        QCOMPARE(liveLoadOrderMismatch(p, e.game), QString());

        // Something outside Solero (the game itself, a tool) rewrote the order.
        writeFile(e.appdata + "/Plugins.txt", "*Skyrim.esm\n*B.esp\n*A.esp\n");
        const QString detail = liveLoadOrderMismatch(p, e.game);
        QVERIFY2(detail.contains("A.esp") || detail.contains("B.esp"), qPrintable(detail));
    }

    // Only the ENABLED set and its order matter to the game, so a disabled plugin
    // the live file doesn't list must not be reported as a mismatch on its own.
    void mismatchDetectorIgnoresDisabledPlugins() {
        Env e; setup(e);
        Profile p("T", e.profiles);
        p.pluginList().append(plugin("Skyrim.esm", true));
        p.pluginList().append(plugin("Off.esp", false));
        p.pluginList().append(plugin("On.esp", true));

        writeFile(e.appdata + "/Plugins.txt", "*Skyrim.esm\n*On.esp\n");
        QCOMPARE(liveLoadOrderMismatch(p, e.game), QString());
    }
};

QTEST_MAIN(TestProfileArtifacts)
#include "test_ProfileArtifacts.moc"
