#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDirIterator>
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
    QTemporaryDir m_home; // isolates dataRoot() (see initTestCase)
private slots:
    void initTestCase() {
        // dataRoot() resolves via $HOME, and deploy() deploys dataRoot/overwrite
        // as the highest-priority layer. Point $HOME at an empty temp dir so tests
        // that don't inject an Overwrite dir don't pick up the real (populated)
        // ~/.local/share/solero/overwrite folder.
        qputenv("HOME", m_home.path().toLocal8Bit());
    }
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
    void hiddenFile_notDeployed() {
        // A (low prio) and B (high prio) both stage Data/x.dll. Normally B wins.
        // Hiding x.dll IN B makes B no longer provide it, so A's file wins instead.
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/x.dll", "A-content");
        writeFile(stagingRoot + "/bbb/Data/x.dll", "B-content");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry ma; ma.type = EntryType::Mod; ma.id = "aaa"; ma.name = "Low";  ma.enabled = true;
        ModEntry mb; mb.type = EntryType::Mod; mb.id = "bbb"; mb.name = "High"; mb.enabled = true;
        profile.modList().append(ma);
        profile.modList().append(mb);
        profile.setFileHidden("bbb", "Data/x.dll", true); // hide the higher-prio file

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);

        // The deployed file has A's content (B's is hidden).
        QFile f(gameDir + "/Data/x.dll"); QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("A-content"));

        // The record owner is A, not B.
        DeployRecord record = DeployRecord::loadFromFile(DeployEngine::recordPath(gameDir));
        QCOMPARE(record.ownerOf("Data/x.dll"), QString("aaa"));
    }
    void winnerOverride_forcesLowPriorityMod() {
        // A (low) and B (high) stage Data/x.dll; B wins by load order. An override
        // forcing A re-links A's file in the post-pass, so A wins regardless.
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/x.dll", "A-content");
        writeFile(stagingRoot + "/bbb/Data/x.dll", "B-content");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry ma; ma.type = EntryType::Mod; ma.id = "aaa"; ma.name = "Low";  ma.enabled = true;
        ModEntry mb; mb.type = EntryType::Mod; mb.id = "bbb"; mb.name = "High"; mb.enabled = true;
        profile.modList().append(ma);
        profile.modList().append(mb);
        profile.setWinnerOverride("Data/x.dll", "aaa"); // force the low-prio mod

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);

        // Deployed file has A's content though B is higher priority.
        QFile f(gameDir + "/Data/x.dll"); QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("A-content"));

        // Record owner = A; conflict winner = A, with B recorded as a loser.
        DeployRecord record = DeployRecord::loadFromFile(DeployEngine::recordPath(gameDir));
        QCOMPARE(record.ownerOf("Data/x.dll"), QString("aaa"));
        QCOMPARE(result.conflicts.winnerOf("Data/x.dll"), QString("aaa"));
        QVERIFY(result.conflicts.losersOf("Data/x.dll").contains("bbb"));
    }
    void deploy_normalizesDirectoryCaseToSingleVariant() {
        // Two mods stage DIFFERENT files under a same-named dir with different
        // case (textures vs Textures). Wine/Proton is case-insensitive but the
        // Linux game dir is case-sensitive, so a naive deploy creates TWO real
        // sibling dirs and the game only sees one - files in the other vanish.
        // The engine must funnel both into a single on-disk directory casing.
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/textures/landscape/a.dds", "A");
        writeFile(stagingRoot + "/bbb/Textures/architecture/b.dds", "B");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry ma; ma.type = EntryType::Mod; ma.id = "aaa"; ma.name = "Low";  ma.enabled = true;
        ModEntry mb; mb.type = EntryType::Mod; mb.id = "bbb"; mb.name = "High"; mb.enabled = true;
        profile.modList().append(ma);
        profile.modList().append(mb);

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);

        // Exactly one directory under gameDir matches "textures" case-insensitively.
        QStringList dirs = QDir(gameDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        int texCount = 0;
        QString texName;
        for (const auto& d : dirs)
            if (d.compare("textures", Qt::CaseInsensitive) == 0) { ++texCount; texName = d; }
        QCOMPARE(texCount, 1);

        // Both files present under that single dir (subfolders keep their own casing).
        QVERIFY(QFile::exists(gameDir + "/" + texName + "/landscape/a.dds"));
        QVERIFY(QFile::exists(gameDir + "/" + texName + "/architecture/b.dds"));
    }
    void deploy_caseVariantFile_collapsesToOneWinner() {
        // Regression guard: two mods stage the same file with different case;
        // exactly one survives (the higher-priority one), with its content.
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/SKSE/Plugins/QUI.dll", "old");
        writeFile(stagingRoot + "/bbb/Data/SKSE/Plugins/qui.dll", "new");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry ma; ma.type = EntryType::Mod; ma.id = "aaa"; ma.name = "Low";  ma.enabled = true;
        ModEntry mb; mb.type = EntryType::Mod; mb.id = "bbb"; mb.name = "High"; mb.enabled = true;
        profile.modList().append(ma);
        profile.modList().append(mb);

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);

        QStringList dlls = QDir(gameDir + "/Data/SKSE/Plugins")
                               .entryList(QStringList() << "*.dll", QDir::Files);
        int quiCount = 0;
        QString quiName;
        for (const auto& d : dlls)
            if (d.compare("qui.dll", Qt::CaseInsensitive) == 0) { ++quiCount; quiName = d; }
        QCOMPARE(quiCount, 1);
        QFile f(gameDir + "/Data/SKSE/Plugins/" + quiName);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("new"));
    }
    void deploy_preExistingFileFoundDespiteCaseDifference() {
        // A genuine vanilla file sits at capital-S Scripts/. A mod stages the
        // same file under lowercase scripts/. The engine must resolve the
        // destination against the existing casing so it (a) backs up the
        // vanilla original and (b) does not create a second scripts/ dir.
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(gameDir + "/Data/Scripts/foo.pex", "ORIGINAL");
        writeFile(stagingRoot + "/aaa/Data/scripts/foo.pex", "MODDED");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry m; m.type = EntryType::Mod; m.id = "aaa"; m.name = "M"; m.enabled = true;
        profile.modList().append(m);

        DeployEngine engine(gameDir, stagingRoot);
        engine.deploy(profile, DeployMode::Copy);

        // Exactly one case-insensitive "Scripts" dir under Data/ (no split).
        QStringList dirs = QDir(gameDir + "/Data").entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        int scriptsCount = 0;
        QString scriptsName;
        for (const auto& d : dirs)
            if (d.compare("scripts", Qt::CaseInsensitive) == 0) { ++scriptsCount; scriptsName = d; }
        QCOMPARE(scriptsCount, 1);

        // Deployed content is the mod's; vanilla original is in the backup tree.
        { QFile f(gameDir + "/Data/" + scriptsName + "/foo.pex"); QVERIFY(f.open(QIODevice::ReadOnly));
          QCOMPARE(f.readAll(), QByteArray("MODDED")); }
        // Find the backed-up original (case-insensitive walk of the backup tree).
        QString backupFile;
        QDirIterator bit(gameDir + "/.solero-backup", QDir::Files, QDirIterator::Subdirectories);
        while (bit.hasNext()) { QString p = bit.next(); if (p.endsWith("foo.pex", Qt::CaseInsensitive)) backupFile = p; }
        QVERIFY(!backupFile.isEmpty());
        { QFile f(backupFile); QVERIFY(f.open(QIODevice::ReadOnly));
          QCOMPARE(f.readAll(), QByteArray("ORIGINAL")); }

        engine.undeploy(gameDir);

        // Original restored at its (single) Scripts path; no lingering modded file.
        QString restored = gameDir + "/Data/" + scriptsName + "/foo.pex";
        QVERIFY(QFile::exists(restored));
        { QFile f(restored); QVERIFY(f.open(QIODevice::ReadOnly));
          QCOMPARE(f.readAll(), QByteArray("ORIGINAL")); }
        QVERIFY(!QDir(gameDir + "/.solero-backup").exists());
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
