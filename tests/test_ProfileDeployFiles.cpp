#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "core/Profile.h"
#include "core/AppConfig.h"
#include "deploy/DeployEngine.h"
#include "bethini/IniFile.h"
using namespace solero;

// Per-profile INI and save behaviour on deploy, and what happens when the flags are
// toggled off again. The deploy engine writes INIs to AppConfig::documentsDir() (else
// the game dir); each test pins documentsDir to its own temp dir so nothing real is
// touched.
class TestProfileDeployFiles : public QObject {
    Q_OBJECT
    static void writeFile(const QString& p, const QByteArray& c) {
        QDir().mkpath(QFileInfo(p).path());
        QFile f(p); QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(p)); f.write(c);
    }
    struct Env { QTemporaryDir tmp; QString game, staging, docs; };
    static void setup(Env& e) {
        e.game = e.tmp.path() + "/game";       QDir().mkpath(e.game);
        e.staging = e.tmp.path() + "/staging"; QDir().mkpath(e.staging);
        e.docs = e.tmp.path() + "/docs";       QDir().mkpath(e.docs);
        AppConfig::instance().setDocumentsDir(e.docs);
    }
private slots:
    // localSaves ON: the deployed Skyrim.ini gets the per-profile SLocalSavePath and
    // the folder is created.
    void saves_on_writesRedirect() {
        Env e; setup(e);
        Profile p("MyChar", e.tmp.path() + "/profiles");
        p.setLocalSaves(true);
        DeployEngine eng(e.game, e.staging);
        QVERIFY(eng.deploy(p, DeployMode::Copy).success);
        IniFile ini; ini.load(e.docs + "/Skyrim.ini");
        QCOMPARE(ini.value("General", "SLocalSavePath"), QString("Saves\\MyChar\\"));
        QVERIFY(QFileInfo::exists(e.docs + "/Saves/MyChar"));
    }

    // Toggling localSaves off after it was ON strips the redirect again, so saves go
    // back to the shared folder.
    void saves_off_clearsRedirect() {
        Env e; setup(e);
        Profile p("MyChar", e.tmp.path() + "/profiles");
        DeployEngine eng(e.game, e.staging);
        p.setLocalSaves(true);  QVERIFY(eng.deploy(p, DeployMode::Copy).success);
        p.setLocalSaves(false); QVERIFY(eng.deploy(p, DeployMode::Copy).success);
        IniFile ini; ini.load(e.docs + "/Skyrim.ini");
        QVERIFY(!ini.has("General", "SLocalSavePath"));
    }

    // localInis ON: the profile's own Skyrim.ini is deployed into My Games.
    void inis_on_deploysProfileIni() {
        Env e; setup(e);
        Profile p("T", e.tmp.path() + "/profiles");
        writeFile(p.skyrimIniPath(), "[Display]\niSize W=1920\n");
        p.setLocalInis(true);
        DeployEngine eng(e.game, e.staging);
        QVERIFY(eng.deploy(p, DeployMode::Copy).success);
        IniFile ini; ini.load(e.docs + "/Skyrim.ini");
        QCOMPARE(ini.value("Display", "iSize W"), QString("1920"));
    }

    // localInis off: the profile's Skyrim.ini is not pushed; the live one is left alone.
    void inis_off_leavesLiveIni() {
        Env e; setup(e);
        Profile p("T", e.tmp.path() + "/profiles");
        writeFile(p.skyrimIniPath(), "[Display]\niSize W=1920\n"); // profile's copy
        writeFile(e.docs + "/Skyrim.ini", "[Display]\niSize W=800\n"); // live/shared
        p.setLocalInis(false);
        DeployEngine eng(e.game, e.staging);
        QVERIFY(eng.deploy(p, DeployMode::Copy).success);
        IniFile ini; ini.load(e.docs + "/Skyrim.ini");
        QCOMPARE(ini.value("Display", "iSize W"), QString("800")); // unchanged
    }
};
QTEST_MAIN(TestProfileDeployFiles)
#include "test_ProfileDeployFiles.moc"
