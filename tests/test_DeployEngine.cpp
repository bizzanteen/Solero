#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDirIterator>
#include <unistd.h> // geteuid (root can't test unremovable files)
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
        // The conflict index is keyed by the canonical case-folded path.
        QCOMPARE(result.conflicts.winnerOf("data/foo.nif"), QString("bbb"));
        QVERIFY(result.conflicts.losersOf("data/foo.nif").contains("aaa"));
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

        // The record holds exactly one entry for the case-variant file (no
        // duplicate). Count only the dll path - generated artifacts like
        // Plugins.txt/loadorder.txt may also be recorded under gameDir in tests
        // (no Proton appdata), so don't assert on the total record count.
        DeployRecord record = DeployRecord::loadFromFile(
            DeployEngine::recordPath(gameDir));
        int dllEntries = 0;
        for (const QString& p : record.allPaths())
            if (p.endsWith(".dll", Qt::CaseInsensitive)) ++dllEntries;
        QCOMPARE(dllEntries, 1);
    }
    void caseVariantConflict_winnerOnCanonicalKey() {
        // Regression (conflict UI correctness): mod A stages textures/foo.dds, mod B
        // (higher priority) stages Textures/foo.dds. On disk these collapse case-
        // insensitively; the conflict index must report one entry on the canonical
        // (case-folded) key with B winning and A as a loser - not a stale
        // setWinner(A) under the lowercase variant.
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/textures/foo.dds", "A");
        writeFile(stagingRoot + "/bbb/Data/Textures/foo.dds", "B");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry ma; ma.type = EntryType::Mod; ma.id = "aaa"; ma.name = "A"; ma.enabled = true;
        ModEntry mb; mb.type = EntryType::Mod; mb.id = "bbb"; mb.name = "B"; mb.enabled = true;
        profile.modList().append(ma);
        profile.modList().append(mb);

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);

        // Canonical key (case-folded): B wins, A is the loser. Exactly one entry.
        const QString key = "data/textures/foo.dds";
        QCOMPARE(result.conflicts.winnerOf(key), QString("bbb"));
        QVERIFY(result.conflicts.losersOf(key).contains("aaa"));
        QCOMPARE(result.conflicts.conflictedPaths().size(), 1);
        // No stale entry under a case-variant of the key claiming A won.
        QVERIFY(!result.conflicts.winningFilesOf("aaa").contains(key));
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
        QCOMPARE(result.conflicts.winnerOf("data/x.dll"), QString("aaa"));
        QVERIFY(result.conflicts.losersOf("data/x.dll").contains("bbb"));
    }
    void winnerOverride_linkFailureCountsAsDeployFailure() {
        // A forced conflict winner that can't be linked must fail the deploy (so the
        // user learns their override didn't apply) instead of silently succeeding.
        // A missing staged source is *skipped* by design; to exercise the actual
        // link-failure path we stage the override's source as a DIRECTORY, which
        // exists (passes validation) but can't be hard-linked/copied as a file.
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        QDir().mkpath(stagingRoot + "/aaa/Data/x.dll"); // source is a dir -> unlinkable

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry ma; ma.type = EntryType::Mod; ma.id = "aaa"; ma.name = "Low"; ma.enabled = true;
        profile.modList().append(ma);
        profile.setWinnerOverride("Data/x.dll", "aaa");

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);

        // The failed forced-winner link is counted, so the deploy is reported failed
        // with an errorMessage the UI can surface.
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains("failed to deploy"));
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
    // the deploy record is persisted incrementally (per-mod flush), so at any
    // interruption point the on-disk record already covers what has been linked.
    // We observe this via the progress callback: right after the first mod is
    // linked the record file must already list its file. Before the fix the record
    // was written only at the very end, so this read would find nothing.
    void interruptedDeploy_recordCoversLinkedFilesMidway() {
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/a.nif", "A");
        writeFile(stagingRoot + "/bbb/Data/b.nif", "B");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry ma; ma.type = EntryType::Mod; ma.id = "aaa"; ma.name = "A"; ma.enabled = true;
        ModEntry mb; mb.type = EntryType::Mod; mb.id = "bbb"; mb.name = "B"; mb.enabled = true;
        profile.modList().append(ma);
        profile.modList().append(mb);

        DeployEngine engine(gameDir, stagingRoot);
        bool checkedMidway = false;
        engine.deploy(profile, DeployMode::Copy, [&](int done, int /*total*/) {
            if (done == 1 && QFile::exists(gameDir + "/Data/a.nif")) {
                DeployRecord rec = DeployRecord::loadFromFile(
                    DeployEngine::recordPath(gameDir));
                QVERIFY2(rec.allPaths().contains("Data/a.nif"),
                         "record must be flushed per-mod");
                checkedMidway = true;
            }
        });
        QVERIFY(checkedMidway);
    }

    // a deploy killed mid-way leaves game-dir files plus an incrementally
    // flushed record covering exactly them. A subsequent undeploy must remove every
    // linked file - no orphans survive the interruption.
    void interruptedDeploy_partialRecordFullyRemovedByUndeploy() {
        QTemporaryDir tmp;
        QString gameDir = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(gameDir + "/Data/a.nif", "A");
        writeFile(gameDir + "/Data/b.nif", "B");
        DeployRecord rec;
        rec.add("Data/a.nif", "aaa");
        rec.add("Data/b.nif", "bbb");
        QVERIFY(rec.saveToFile(DeployEngine::recordPath(gameDir)));

        DeployEngine engine(gameDir, tmp.path() + "/staging");
        QVERIFY(engine.undeploy(gameDir)); // clean -> returns true
        QVERIFY(!QFile::exists(gameDir + "/Data/a.nif"));
        QVERIFY(!QFile::exists(gameDir + "/Data/b.nif"));
    }

    // undeploy returns false (not an unconditional true) when a recorded file
    // can't be removed. We block removal by dropping write permission on the parent
    // directory so the unlink is denied. The record is kept for a later retry.
    void undeploy_returnsFalseWhenFileCannotBeRemoved() {
        if (::geteuid() == 0) QSKIP("root bypasses directory permissions");
        QTemporaryDir tmp;
        QString gameDir = tmp.path() + "/game";
        QString lockedDir = gameDir + "/Data/locked";
        QDir().mkpath(lockedDir);
        writeFile(lockedDir + "/stuck.nif", "X");
        DeployRecord rec;
        rec.add("Data/locked/stuck.nif", "aaa");
        QVERIFY(rec.saveToFile(DeployEngine::recordPath(gameDir)));

        // r-x on the parent dir -> the file inside cannot be unlinked.
        QVERIFY(QFile::setPermissions(lockedDir, QFile::ReadOwner | QFile::ExeOwner));

        DeployEngine engine(gameDir, tmp.path() + "/staging");
        const bool ok = engine.undeploy(gameDir);

        // Restore write so QTemporaryDir can clean up, then assert.
        QFile::setPermissions(lockedDir,
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY(!ok);
        QVERIFY(QFile::exists(gameDir + "/Data/locked/stuck.nif")); // still stuck
        // Record kept so a later undeploy can retry.
        QVERIFY(QFile::exists(DeployEngine::recordPath(gameDir)));
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

    // The managed shader cache (profile state, not a list mod) deploys last, so its
    // copy of a conflicting ShaderCache file wins over a real mod's.
    void managedShaderCache_deploysLastAndWins() {
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/aaa/Data/ShaderCache/x.bin", "MOD");
        writeFile(stagingRoot + "/cachefolder/Data/ShaderCache/x.bin", "CACHE");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry mod; mod.type = EntryType::Mod; mod.id = "aaa";
        mod.name = "RealMod"; mod.enabled = true; // staging folder falls back to id
        profile.modList().append(mod);
        profile.shaderCache().managed = true;
        profile.shaderCache().folders.insert("default", "cachefolder"); // no CS mod -> key="default"
        profile.save();

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);
        QFile f(gameDir + "/Data/ShaderCache/x.bin");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("CACHE")); // cache deployed last -> wins
    }

    // Only the active CS version's cache folder deploys. The CS mod's nexusFileId
    // ("f1") is the active key, so folderFor("f1")=CacheA is deployed and the other
    // version's folder (CacheB, key "f2") is never touched.
    void shaderCacheDeploysOnlyMatchingKey() {
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/CacheA/Data/ShaderCache/a.bin", "A");
        writeFile(stagingRoot + "/CacheB/Data/ShaderCache/b.bin", "B");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry cs; cs.type = EntryType::Mod; cs.id = "cs";
        cs.name = "Community Shaders"; cs.enabled = true; cs.nexusFileId = "f1";
        profile.modList().append(cs);
        profile.shaderCache().managed = true;
        profile.shaderCache().folders.insert("f1", "CacheA"); // active key
        profile.shaderCache().folders.insert("f2", "CacheB"); // other version
        profile.save();

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);
        QVERIFY(QFile::exists(gameDir + "/Data/ShaderCache/a.bin"));  // active key's cache
        QVERIFY(!QFile::exists(gameDir + "/Data/ShaderCache/b.bin")); // never the wrong version
    }

    // No folder for the active CS key -> deploy NOTHING (never a different version's
    // cache). The active key "f1" has no folder entry, so no ShaderCache dir is
    // created in the game dir at all.
    void shaderCacheMismatchDeploysNothing() {
        QTemporaryDir tmp;
        QString stagingRoot = tmp.path() + "/staging";
        QString gameDir     = tmp.path() + "/game";
        QDir().mkpath(gameDir);
        writeFile(stagingRoot + "/CacheA/Data/ShaderCache/a.bin", "A");

        Profile profile("Test", tmp.path() + "/profiles");
        ModEntry cs; cs.type = EntryType::Mod; cs.id = "cs";
        cs.name = "Community Shaders"; cs.enabled = true; cs.nexusFileId = "f1";
        profile.modList().append(cs);
        profile.shaderCache().managed = true;
        profile.shaderCache().folders.insert("f9", "CacheA"); // key mismatch -> no active folder
        profile.save();

        DeployEngine engine(gameDir, stagingRoot);
        auto result = engine.deploy(profile, DeployMode::Copy);
        QVERIFY(result.success);
        QVERIFY(!QDir(gameDir + "/Data/ShaderCache").exists()); // nothing deployed
    }
};
QTEST_MAIN(TestDeployEngine)
#include "test_DeployEngine.moc"
