// Orphaned deployed files. Two halves of one invariant:
//
//  1. The deploy record must never be pruned of a file that is still physically
//     in the game dir. If it is, and the deploy stops there (crash, OOM kill,
//     power loss), the file becomes permanently invisible to Solero - no later
//     deploy or undeploy can find it, because both work from the record.
//  2. A deploy must clean up files an EARLIER deploy already orphaned that way,
//     since users have them on disk today.
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include "deploy/DeployEngine.h"
#include "deploy/DeployRecord.h"
#include "deploy/DeployJournal.h"
#include "core/Profile.h"
using namespace solero;

static void writeFile(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path); f.open(QIODevice::WriteOnly); f.write(content);
}

static ModEntry mod(const QString& id, const QString& name, bool enabled = true) {
    ModEntry m; m.type = EntryType::Mod; m.id = id; m.name = name; m.enabled = enabled;
    return m;
}

// Relative paths of the real files in the game dir, excluding Solero's own
// metadata (the record and the .solero-backup tree).
static QStringList gameFiles(const QString& dir) {
    QStringList out;
    const QString norm = QDir::cleanPath(dir);
    QDirIterator it(norm, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString rel = it.next().mid(norm.length() + 1);
        if (rel.startsWith(".solero")) continue;
        out << rel;
    }
    out.sort();
    return out;
}

// Drop every path a given mod owns from the on-disk record, leaving the files
// themselves in place. This is exactly the state a deploy leaves behind when it
// flushes the pruned record and then stops before the stale sweep runs.
static void forgetModInRecord(const QString& game, const QString& modId) {
    const QString path = DeployEngine::recordPath(game);
    DeployRecord rec = DeployRecord::loadFromFile(path);
    for (const QString& rel : rec.allPaths())
        if (rec.ownerOf(rel) == modId) rec.remove(rel);
    QVERIFY(rec.saveToFile(path));
}

class TestDeployOrphans : public QObject {
    Q_OBJECT
    QTemporaryDir m_home;
private slots:
    void initTestCase() { qputenv("HOME", m_home.path().toLocal8Bit()); }

    // The record on disk must cover every file in the game dir at every point a
    // deploy could be interrupted. Sampled from the progress callback, which fires
    // after each mod - i.e. after the record flushes that pruning happens in.
    void recordNeverForgetsAFileThatIsStillInTheGameDir() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/staging";
        const QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");
        writeFile(staging + "/B/Data/b1.esp", "bee");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.modList().append(mod("B", "ModB"));
        p.save();

        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);

        // Disable B and add a mod that actually links something, so the deploy
        // flushes the record mid-loop while B's file is still on disk.
        p.modList().findById("B")->enabled = false;
        writeFile(staging + "/C/Data/c1.esp", "see");
        p.modList().append(mod("C", "ModC"));
        p.save();

        QStringList forgotten;
        auto r = e.deploy(p, DeployMode::HardLink, [&](int, int) {
            const DeployRecord onDisk =
                DeployRecord::loadFromFile(DeployEngine::recordPath(game));
            for (const QString& rel : gameFiles(game))
                if (!onDisk.contains(rel) && !forgotten.contains(rel)) forgotten << rel;
        });
        QVERIFY(r.success);
        QVERIFY2(forgotten.isEmpty(),
                 qPrintable("the record dropped file(s) that were still deployed: "
                            + forgotten.join(", ")));
    }

    // The plain case, for both an asset and a plugin: disable, deploy, and the
    // files are gone from the game dir AND from the record.
    void disablingAModRemovesItsFilesFromDiskAndFromTheRecord() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/staging";
        const QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");
        writeFile(staging + "/B/Data/b1.esp", "bee");
        writeFile(staging + "/B/Data/meshes/b1.nif", "mesh");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.modList().append(mod("B", "ModB"));
        p.save();

        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);

        p.modList().findById("B")->enabled = false;
        p.save();
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);

        QVERIFY(!QFile::exists(game + "/Data/b1.esp"));
        QVERIFY(!QFile::exists(game + "/Data/meshes/b1.nif"));
        const DeployRecord rec =
            DeployRecord::loadFromFile(DeployEngine::recordPath(game));
        QVERIFY(!rec.contains("Data/b1.esp"));
        QVERIFY(!rec.contains("Data/meshes/b1.nif"));
        QVERIFY(QFile::exists(game + "/Data/a1.esp"));
    }

    // The state users already have: a previous deploy pruned the record but never
    // unlinked the files. The record can no longer see them, so the fix has to be
    // structural - a hardlink into the game dir from a mod that isn't deployed.
    void deployCleansUpFilesAnEarlierDeployOrphaned() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/staging";
        const QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");
        writeFile(staging + "/B/Data/b1.esp", "bee");
        writeFile(staging + "/B/Data/meshes/b1.nif", "mesh");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.modList().append(mod("B", "ModB"));
        p.save();

        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);

        forgetModInRecord(game, "B");
        p.modList().findById("B")->enabled = false;
        p.save();

        auto r = e.deploy(p, DeployMode::HardLink);
        QVERIFY(r.success);
        QVERIFY2(!QFile::exists(game + "/Data/b1.esp"), "orphaned plugin survived the deploy");
        QVERIFY2(!QFile::exists(game + "/Data/meshes/b1.nif"), "orphaned asset survived the deploy");
        QVERIFY(QFile::exists(game + "/Data/a1.esp"));
    }

    // Same, for a mod removed from the profile outright rather than disabled -
    // its staging folder is still on disk but nothing references it.
    void deployCleansUpFilesLeftByAModThatIsGoneFromTheProfile() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/staging";
        const QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");
        writeFile(staging + "/B/Data/b1.esp", "bee");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.modList().append(mod("B", "ModB"));
        p.save();

        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);

        forgetModInRecord(game, "B");
        p.modList().remove("B");
        p.save();

        QVERIFY(e.deploy(p, DeployMode::HardLink).success);
        QVERIFY(!QFile::exists(game + "/Data/b1.esp"));
        QVERIFY(QFile::exists(game + "/Data/a1.esp"));
    }

    // What an interrupted deploy actually leaves: the record from before it, plus a
    // journal of what it managed to link. Both halves have to come back, or the
    // files linked by the interrupted run are untracked from then on.
    void anInterruptedDeployIsRecoveredFromItsJournal() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/staging";
        const QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.save();

        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);

        // Stand in for the tail of an interrupted deploy: a file linked into the
        // game dir and journalled, with the record still describing the run before.
        const QString recPath = DeployEngine::recordPath(game);
        writeFile(game + "/Data/late.esp", "late");
        {
            DeployJournal j;
            QVERIFY(j.begin(recPath));
            j.append("Data/late.esp", "A", 4, 1234);
            j.flush();
        }

        const DeployRecord rec = DeployRecord::loadFromFile(recPath);
        QVERIFY2(rec.contains("Data/a1.esp"), "lost the record's own entries");
        QVERIFY2(rec.contains("Data/late.esp"), "lost the journalled entry");
        QCOMPARE(rec.ownerOf("Data/late.esp"), QStringLiteral("A"));

        // And a clean undeploy takes the journal with the record, so its entries
        // can't come back to haunt the next deploy.
        QVERIFY(e.undeploy(game));
        QVERIFY(!QFile::exists(recPath));
        QVERIFY(!QFile::exists(DeployJournal::pathFor(recPath)));
        QVERIFY(DeployRecord::loadFromFile(recPath).allPaths().isEmpty());
    }

    // Output mods (Nemesis, PGPatcher, Radium, Synthesis) are the highest-stakes
    // case for the sweep: their content exists nowhere else, and deleting Nemesis's
    // behaviour files breaks every animation in the game. So an enabled output mod's
    // files must survive even when the record has lost track of them - the sweep may
    // never fall back on "in the game dir, not in the record" as its test.
    void theSweepNeverTouchesAnEnabledOutputModsFiles() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/staging";
        const QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");
        // Captured tool output: staged only here, provided by no other mod.
        writeFile(staging + "/Nemesis Output/Data/meshes/actors/character/behaviors/0_master.hkx",
                  "behaviour");
        writeFile(staging + "/Nemesis Output/Data/meshes/animationdatasinglefile.txt", "anim");
        // A disabled mod, so the sweep has something to walk and actually runs.
        writeFile(staging + "/Old/Data/old.esp", "old");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.modList().append(mod("NEM", "Nemesis Output"));
        p.modList().append(mod("Old", "Old", false));
        p.modList().findById("NEM")->stagingFolder = "Nemesis Output";
        p.save();

        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);
        const QString behaviour = game + "/Data/meshes/actors/character/behaviors/0_master.hkx";
        QVERIFY(QFile::exists(behaviour));

        // Record drift: whatever the cause, the record no longer knows these are
        // Nemesis's. The mod is still enabled, so they are still live game content.
        forgetModInRecord(game, "NEM");
        auto r = e.deploy(p, DeployMode::HardLink);
        QVERIFY(r.success);
        QVERIFY2(QFile::exists(behaviour), "swept a live Nemesis behaviour file");
        QVERIFY2(QFile::exists(game + "/Data/meshes/animationdatasinglefile.txt"),
                 "swept a live Nemesis output file");
    }

    // Loose files that no mod provides - an old tool run that wrote straight into
    // the game folder, or a leftover from a previous manager. Solero does not own
    // them, so it must not delete them just because they are unrecorded.
    void theSweepLeavesUnmanagedLooseFilesAlone() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/staging";
        const QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(staging + "/A/Data/a1.esp", "one");
        writeFile(staging + "/Old/Data/old.esp", "old");   // disabled, gives the sweep work
        // Provided by nothing in staging, and a single link, so no inode ties it to
        // Solero. Mirrors the ancient loose behaviour files in a real game dir.
        writeFile(game + "/Data/meshes/actors/character/_1stperson/behaviors/0_master.hkx",
                  "ancient");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.modList().append(mod("Old", "Old", false));
        p.save();

        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);
        QVERIFY2(QFile::exists(game + "/Data/meshes/actors/character/_1stperson/behaviors/0_master.hkx"),
                 "swept a loose file Solero never deployed");
    }

    // The sweep is structural, so it has to be narrow: a file this deploy legitimately
    // put there must survive even when the record lost track of it, and a vanilla file
    // that never came from staging must never be touched.
    void theSweepLeavesDeployedAndVanillaFilesAlone() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/staging";
        const QString game    = tmp.path() + "/game";
        QDir().mkpath(game);
        writeFile(game + "/Data/Skyrim.esm", "vanilla");
        writeFile(staging + "/A/Data/a1.esp", "one");
        writeFile(staging + "/B/Data/b1.esp", "bee");

        Profile p("T", tmp.path() + "/profiles");
        p.modList().append(mod("A", "ModA"));
        p.modList().append(mod("B", "ModB"));
        p.save();

        DeployEngine e(game, staging);
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);

        // B stays enabled, so this deploy re-records its files - the sweep must not
        // mistake them for orphans just because the previous record forgot them.
        forgetModInRecord(game, "B");
        QVERIFY(e.deploy(p, DeployMode::HardLink).success);

        QVERIFY2(QFile::exists(game + "/Data/b1.esp"), "swept a file its mod still provides");
        QVERIFY(QFile::exists(game + "/Data/a1.esp"));
        QVERIFY2(QFile::exists(game + "/Data/Skyrim.esm"), "swept a vanilla file");
    }
};

QTEST_MAIN(TestDeployOrphans)
#include "test_DeployOrphans.moc"
