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
