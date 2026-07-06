// incremental deploy: only add/remove the files that changed since the
// last deploy, producing a game-dir state identical to a full redeploy.
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDirIterator>
#include "deploy/DeployEngine.h"
#include "deploy/DeployRecord.h"
#include "core/Profile.h"
using namespace solero;

static void writeFile(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path); f.open(QIODevice::WriteOnly); f.write(content);
}

// Sorted relative paths of real files under dir, excluding Solero metadata (the
// deploy record and the backup tree) - the "observable" deployed set.
static QStringList deployedSet(const QString& dir) {
    QStringList out;
    const QString norm = QDir::cleanPath(dir);
    QDirIterator it(norm, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString rel = it.next().mid(norm.length() + 1);
        if (rel.startsWith(".solero")) continue;   // record file + .solero-backup tree
        out << rel;
    }
    out.sort();
    return out;
}

static ModEntry mod(const QString& id, const QString& name, bool enabled = true) {
    ModEntry m; m.type = EntryType::Mod; m.id = id; m.name = name; m.enabled = enabled;
    return m;
}

class TestDeployIncremental : public QObject {
    Q_OBJECT
    QTemporaryDir m_home;
private slots:
    void initTestCase() { qputenv("HOME", m_home.path().toLocal8Bit()); }

    // Deploying the same profile twice links nothing the second time.
    void reDeployIdentical_linksNothing() {
        QTemporaryDir tmp;
        QString staging = tmp.path() + "/staging";
        QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");
        writeFile(staging + "/A/Data/a2.nif", "two");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.save();

        DeployEngine e(game, staging);
        auto r1 = e.deploy(p, DeployMode::Copy);
        QVERIFY(r1.success);
        QCOMPARE(r1.filesLinked, 2);

        auto r2 = e.deploy(p, DeployMode::Copy);
        QVERIFY(r2.success);
        QVERIFY(r2.incremental);
        QCOMPARE(r2.filesLinked, 0);
        QCOMPARE(r2.filesRemoved, 0);
        QCOMPARE(r2.filesUnchanged, 2);
        QVERIFY(QFile::exists(game + "/Data/a1.esp"));
    }

    // Enabling a second mod links only that mod's files.
    void enableOneMod_linksOnlyItsFiles() {
        QTemporaryDir tmp;
        QString staging = tmp.path() + "/staging";
        QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");
        writeFile(staging + "/B/Data/b1.esp", "bee");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.save();
        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::Copy).success);

        p.modList().append(mod("B", "ModB"));
        p.save();
        auto r = e.deploy(p, DeployMode::Copy);
        QVERIFY(r.success);
        QVERIFY(r.incremental);
        QCOMPARE(r.filesLinked, 1);      // only B's file
        QCOMPARE(r.filesRemoved, 0);
        QCOMPARE(r.filesUnchanged, 1);   // A's file untouched
        QVERIFY(QFile::exists(game + "/Data/b1.esp"));
    }

    // Disabling a mod removes its unique files and nothing else.
    void disableOneMod_removesItsUniqueFiles() {
        QTemporaryDir tmp;
        QString staging = tmp.path() + "/staging";
        QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");
        writeFile(staging + "/B/Data/b1.esp", "bee");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.modList().append(mod("B", "ModB"));
        p.save();
        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::Copy).success);
        QVERIFY(QFile::exists(game + "/Data/b1.esp"));

        p.modList().findById("B")->enabled = false;
        p.save();
        auto r = e.deploy(p, DeployMode::Copy);
        QVERIFY(r.success);
        QVERIFY(r.incremental);
        QCOMPARE(r.filesRemoved, 1);
        QCOMPARE(r.filesLinked, 0);
        QVERIFY(!QFile::exists(game + "/Data/b1.esp"));
        QVERIFY(QFile::exists(game + "/Data/a1.esp"));
    }

    // Editing a mod's staged file (changed size) relinks just that file.
    void reinstallBumpsFingerprint_relinks() {
        QTemporaryDir tmp;
        QString staging = tmp.path() + "/staging";
        QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");
        writeFile(staging + "/A/Data/a2.nif", "two");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.save();
        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::Copy).success);

        // Change a1's content+size; a2 unchanged.
        writeFile(staging + "/A/Data/a1.esp", "one-plus-more-bytes");
        auto r = e.deploy(p, DeployMode::Copy);
        QVERIFY(r.success);
        QVERIFY(r.incremental);
        QCOMPARE(r.filesLinked, 1);      // a1 relinked
        QCOMPARE(r.filesUnchanged, 1);   // a2 skipped
        QFile f(game + "/Data/a1.esp"); f.open(QIODevice::ReadOnly);
        QCOMPARE(f.readAll(), QByteArray("one-plus-more-bytes"));
    }

    // Changing the deploy mode forces a full redeploy (every file's form changes).
    void modeChange_forcesFull() {
        QTemporaryDir tmp;
        QString staging = tmp.path() + "/staging";
        QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.save();
        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::Copy).success);

        auto r = e.deploy(p, DeployMode::HardLink);
        QVERIFY(r.success);
        QVERIFY(!r.incremental);         // forced full
        QCOMPARE(r.filesLinked, 1);
    }

    // A legacy v1 record (no fingerprints) forces one full redeploy, upgrading it.
    void legacyV1Record_forcesFull() {
        QTemporaryDir tmp;
        QString staging = tmp.path() + "/staging";
        QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.save();
        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::Copy).success);

        // Overwrite the v2 record with a legacy flat map.
        QString rec = DeployEngine::recordPath(game);
        writeFile(rec, R"({"Data/a1.esp":"A"})");

        auto r = e.deploy(p, DeployMode::Copy);
        QVERIFY(r.success);
        QVERIFY(!r.incremental);
        // Record upgraded back to v2.
        QCOMPARE(DeployRecord::loadFromFile(rec).version(), 2);
    }

    // A pre-existing (vanilla) original is backed up on first deploy and restored
    // when the covering mod is later disabled - across incremental deploys.
    void preexistingOriginal_restoredOnDisable() {
        QTemporaryDir tmp;
        QString staging = tmp.path() + "/staging";
        QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(game + "/Data/shared.esp", "VANILLA");   // pre-existing original
        writeFile(staging + "/A/Data/shared.esp", "MODDED");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.save();
        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::Copy).success);
        { QFile f(game + "/Data/shared.esp"); f.open(QIODevice::ReadOnly);
          QCOMPARE(f.readAll(), QByteArray("MODDED")); }

        // A second identical deploy must not re-backup A's own file as an original.
        QVERIFY(e.deploy(p, DeployMode::Copy).success);
        { QFile f(game + "/Data/shared.esp"); f.open(QIODevice::ReadOnly);
          QCOMPARE(f.readAll(), QByteArray("MODDED")); }

        // Disable A -> vanilla original restored.
        p.modList().findById("A")->enabled = false;
        p.save();
        QVERIFY(e.deploy(p, DeployMode::Copy).success);
        QVERIFY(QFile::exists(game + "/Data/shared.esp"));
        QFile f(game + "/Data/shared.esp"); f.open(QIODevice::ReadOnly);
        QCOMPARE(f.readAll(), QByteArray("VANILLA"));
    }

    // The oracle: after a sequence of incremental edits, the deployed set equals a
    // full redeploy of the final profile into a clean game dir.
    void incrementalResult_equalsFullRedeploy() {
        QTemporaryDir tmp;
        QString staging = tmp.path() + "/staging";
        QString game    = tmp.path() + "/game";
        QString game2   = tmp.path() + "/game2"; // oracle: clean full deploy
        QDir().mkpath(game); QDir().mkpath(game2);
        writeFile(staging + "/A/Data/a1.esp", "a1");
        writeFile(staging + "/B/Data/b1.esp", "b1");
        writeFile(staging + "/B/Data/shared.nif", "b-shared");
        writeFile(staging + "/C/Data/shared.nif", "c-shared"); // conflicts with B

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.modList().append(mod("B", "ModB"));
        p.modList().append(mod("C", "ModC"));
        p.save();

        DeployEngine inc(game, staging);
        QVERIFY(inc.deploy(p, DeployMode::Copy).success);
        // Mutate: disable A, edit B's file, keep C (wins shared.nif).
        p.modList().findById("A")->enabled = false;
        writeFile(staging + "/B/Data/b1.esp", "b1-edited-longer");
        p.save();
        QVERIFY(inc.deploy(p, DeployMode::Copy).success);

        // Oracle: full deploy of the same final profile into a clean dir.
        DeployEngine full(game2, staging);
        QVERIFY(full.deploy(p, DeployMode::Copy).success);

        QCOMPARE(deployedSet(game), deployedSet(game2));
        // Winner of the conflict is identical too.
        QFile fi(game + "/Data/shared.nif");  fi.open(QIODevice::ReadOnly);
        QFile fo(game2 + "/Data/shared.nif"); fo.open(QIODevice::ReadOnly);
        QCOMPARE(fi.readAll(), fo.readAll());
    }
};
QTEST_MAIN(TestDeployIncremental)
#include "test_DeployIncremental.moc"
