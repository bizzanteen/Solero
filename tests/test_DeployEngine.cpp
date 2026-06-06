#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "deploy/DeployEngine.h"
#include "deploy/DeployRecord.h"
#include "deploy/ConflictIndex.h"
#include "core/Profile.h"
using namespace solero;

static void writeFile(const QString& path, const QByteArray& content = "data") {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path); f.open(QIODevice::WriteOnly); f.write(content);
}

class TestDeployEngine : public QObject {
    Q_OBJECT
private slots:
    void deploy_copiesModFilesToGameDir() {
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/foo.nif", "mesh");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry mod; mod.type = EntryType::Mod; mod.id = "aaa";
        mod.name = "TestMod"; mod.enabled = true;
        profile.modList().append(mod);
        profile.save();

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);
        QVERIFY(QFile::exists(gameDir + "/Data/foo.nif"));
    }
    void deploy_buildsConflictIndex() {
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/foo.nif", "low");
        writeFile(stagingRoot + "/bbb/Data/foo.nif", "high");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry ma; ma.type = EntryType::Mod; ma.id = "aaa"; ma.name = "Low";  ma.enabled = true;
        ModEntry mb; mb.type = EntryType::Mod; mb.id = "bbb"; mb.name = "High"; mb.enabled = true;
        profile.modList().append(ma);
        profile.modList().append(mb);

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);
        QCOMPARE(result.conflicts.winnerOf("Data/foo.nif"), QString("bbb"));
        QVERIFY(result.conflicts.losersOf("Data/foo.nif").contains("aaa"));
        QFile f(gameDir + "/Data/foo.nif"); f.open(QIODevice::ReadOnly);
        QCOMPARE(f.readAll(), QByteArray("high"));
    }
    void undeploy_removesDeployedFiles() {
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/bar.nif");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry m; m.type = EntryType::Mod; m.id = "aaa"; m.name = "M"; m.enabled = true;
        profile.modList().append(m);

        DeployEngine engine(gameDir, stagingRoot);
        engine.deploy(profile, DeployMode::Copy);
        QVERIFY(QFile::exists(gameDir + "/Data/bar.nif"));

        engine.undeploy(gameDir);
        QVERIFY(!QFile::exists(gameDir + "/Data/bar.nif"));
    }
    void disabledMod_notDeployed() {
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/skip.nif");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry m; m.type = EntryType::Mod; m.id = "aaa"; m.name = "Disabled"; m.enabled = false;
        profile.modList().append(m);

        DeployEngine engine(gameDir, stagingRoot);
        engine.deploy(profile, DeployMode::Copy);
        QVERIFY(!QFile::exists(gameDir + "/Data/skip.nif"));
    }
    void preExistingFile_backedUpAndRestoredOnUndeploy() {
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        // A genuine pre-existing (vanilla) file already in the game dir.
        writeFile(gameDir + "/Data/vanilla.esm", "ORIGINAL");
        // A mod that ships a file at the same path.
        writeFile(stagingRoot + "/aaa/Data/vanilla.esm", "MODDED");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry m; m.type = EntryType::Mod; m.id = "aaa"; m.name = "M"; m.enabled = true;
        profile.modList().append(m);

        DeployEngine engine(gameDir, stagingRoot);
        engine.deploy(profile, DeployMode::Copy);

        // Mod file is now in place; original is preserved in the backup tree.
        { QFile f(gameDir + "/Data/vanilla.esm"); f.open(QIODevice::ReadOnly);
          QCOMPARE(f.readAll(), QByteArray("MODDED")); }
        QVERIFY(QFile::exists(gameDir + "/.solero-backup/Data/vanilla.esm"));

        engine.undeploy(gameDir);

        // Original restored; backup tree gone.
        QVERIFY(QFile::exists(gameDir + "/Data/vanilla.esm"));
        { QFile f(gameDir + "/Data/vanilla.esm"); f.open(QIODevice::ReadOnly);
          QCOMPARE(f.readAll(), QByteArray("ORIGINAL")); }
        QVERIFY(!QDir(gameDir + "/.solero-backup").exists());
    }
    void caseVariantConflict_resolvedToSingleWinner() {
        // Wine/Proton is case-insensitive: two mods staging the same logical
        // file with different case (e.g. base "QUI" ships QUI.dll, the update
        // ships qui.dll) must collapse to one file - the later/higher-priority
        // mod's - matching Windows mod-manager behaviour. Otherwise both land
        // and SKSE loads the plugin twice.
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/SKSE/Plugins/Foo.dll", "v3-base");
        writeFile(stagingRoot + "/bbb/Data/SKSE/Plugins/foo.dll", "v4-update");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry ma; ma.type = EntryType::Mod; ma.id = "aaa"; ma.name = "Base";   ma.enabled = true;
        ModEntry mb; mb.type = EntryType::Mod; mb.id = "bbb"; mb.name = "Update"; mb.enabled = true;
        profile.modList().append(ma);
        profile.modList().append(mb);

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);

        // Exactly one dll lands (case-insensitively) - the stale variant is gone.
        QStringList dlls = QDir(gameDir + "/Data/SKSE/Plugins")
                               .entryList(QStringList() << "*.dll", QDir::Files);
        QCOMPARE(dlls.size(), 1);

        // The survivor is the later mod's file with its content.
        QFile f(gameDir + "/Data/SKSE/Plugins/" + dlls.first());
        f.open(QIODevice::ReadOnly);
        QCOMPARE(f.readAll(), QByteArray("v4-update"));

        // The record holds exactly one entry for this path (no duplicate).
        DeployRecord record = DeployRecord::loadFromFile(
            DeployEngine::recordPath(gameDir));
        QCOMPARE(record.count(), 1);
    }
    void redeploy_removesOrphanedFiles() {
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/keep.nif");
        writeFile(stagingRoot + "/bbb/Data/orphan.nif");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry ma; ma.type = EntryType::Mod; ma.id = "aaa"; ma.name = "Keep";   ma.enabled = true;
        ModEntry mb; mb.type = EntryType::Mod; mb.id = "bbb"; mb.name = "Orphan"; mb.enabled = true;
        profile.modList().append(ma);
        profile.modList().append(mb);

        DeployEngine engine(gameDir, stagingRoot);
        engine.deploy(profile, DeployMode::Copy);
        QVERIFY(QFile::exists(gameDir + "/Data/orphan.nif"));

        // Disable bbb and re-deploy - orphan.nif must be removed
        profile.modList().setEnabled("bbb", false);
        engine.deploy(profile, DeployMode::Copy);
        QVERIFY(QFile::exists(gameDir + "/Data/keep.nif"));
        QVERIFY(!QFile::exists(gameDir + "/Data/orphan.nif"));
    }
};
QTEST_MAIN(TestDeployEngine)
#include "test_DeployEngine.moc"
