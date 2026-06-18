#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "core/OutputModMigration.h"
#include "core/ModList.h"
#include "core/Types.h"
using namespace solero;

static ModEntry mkMod(const QString& id, const QString& name,
                      const QString& stagingFolder, bool isOutput) {
    ModEntry e;
    e.type = EntryType::Mod;
    e.id = id;
    e.name = name;
    e.stagingFolder = stagingFolder;
    e.isOutputMod = isOutput;
    e.enabled = true;
    return e;
}

class TestOutputModMigration : public QObject {
    Q_OBJECT
private:
    QString profilesRoot, stagingDir;

    void writeModlist(const QString& profile, const ModList& ml) {
        QDir().mkpath(profilesRoot + "/" + profile);
        ml.saveToFile(profilesRoot + "/" + profile + "/modlist.json");
    }
    ModList readModlist(const QString& profile) {
        return ModList::loadFromFile(profilesRoot + "/" + profile + "/modlist.json");
    }
    QString folderOf(const ModList& ml, const QString& id) {
        for (const auto& e : ml.entries())
            if (e.id == id) return e.stagingFolder;
        return {};
    }
    void mkStagingFolder(const QString& folder, const QString& marker) {
        QDir().mkpath(stagingDir + "/" + folder + "/Data");
        QFile f(stagingDir + "/" + folder + "/Data/" + marker);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x"); f.close();
    }

private slots:
    void sharedFolder_splitAcrossProfiles() {
        QTemporaryDir tmp;
        profilesRoot = tmp.path() + "/profiles";
        stagingDir   = tmp.path() + "/mods";

        // Two profiles both reference a SHARED bare "PGPatcher Output" output mod.
        // P1 also has an already-prefixed output mod that must be left untouched,
        // plus a normal (non-output) mod that must be ignored.
        ModList p1;
        p1.append(mkMod("id-pg",   "PGPatcher Output", "PGPatcher Output", true));
        p1.append(mkMod("id-dyn",  "Dyndolod Output",  "P1 - Dyndolod Output", true));
        p1.append(mkMod("id-norm", "Some Mod",         "Some Mod", false));
        writeModlist("P1", p1);

        ModList p2;
        p2.append(mkMod("id-pg2", "PGPatcher Output", "PGPatcher Output", true));
        writeModlist("P2", p2);

        // The shared bare folder on disk, with a marker file to prove the content
        // is MOVED (not copied) to whoever claims it first.
        mkStagingFolder("PGPatcher Output", "moved-marker.txt");
        // The already-prefixed folder also exists on disk.
        mkStagingFolder("P1 - Dyndolod Output", "dyn.txt");

        // P1 first (it's the "active" profile) -> it claims the shared content.
        const int n = migrateOutputModsProfileQualified(
            profilesRoot, stagingDir, {"P1", "P2"});
        QCOMPARE(n, 2); // the two PGPatcher output mods; prefixed/non-output skipped

        // P1's PGPatcher mod is now profile-qualified, with the moved content.
        ModList r1 = readModlist("P1");
        QCOMPARE(folderOf(r1, "id-pg"), QString("P1 - PGPatcher Output"));
        QVERIFY(QFile::exists(stagingDir + "/P1 - PGPatcher Output/Data/moved-marker.txt"));
        // The old shared folder was renamed away (moved), not left behind.
        QVERIFY(!QDir(stagingDir + "/PGPatcher Output").exists());

        // The already-prefixed entry is untouched, content intact.
        QCOMPARE(folderOf(r1, "id-dyn"), QString("P1 - Dyndolod Output"));
        QVERIFY(QFile::exists(stagingDir + "/P1 - Dyndolod Output/Data/dyn.txt"));
        // The non-output mod is untouched.
        QCOMPARE(folderOf(r1, "id-norm"), QString("Some Mod"));

        // P2's PGPatcher mod gets a FRESH empty profile-qualified folder (the
        // shared content was already claimed by P1).
        ModList r2 = readModlist("P2");
        QCOMPARE(folderOf(r2, "id-pg2"), QString("P2 - PGPatcher Output"));
        QVERIFY(QDir(stagingDir + "/P2 - PGPatcher Output/Data").exists());
        QVERIFY(!QFile::exists(stagingDir + "/P2 - PGPatcher Output/Data/moved-marker.txt"));

        // Both modlists were backed up before writing.
        QVERIFY(QFile::exists(profilesRoot + "/P1/modlist.json.bak-outputmodmigration"));
        QVERIFY(QFile::exists(profilesRoot + "/P2/modlist.json.bak-outputmodmigration"));
    }

    void idempotent_secondRunNoOp() {
        QTemporaryDir tmp;
        profilesRoot = tmp.path() + "/profiles";
        stagingDir   = tmp.path() + "/mods";

        ModList p1;
        p1.append(mkMod("id-pg", "PGPatcher Output", "PGPatcher Output", true));
        writeModlist("P1", p1);
        mkStagingFolder("PGPatcher Output", "m.txt");

        QCOMPARE(migrateOutputModsProfileQualified(profilesRoot, stagingDir, {"P1"}), 1);
        // Second run: already qualified -> nothing to migrate.
        QCOMPARE(migrateOutputModsProfileQualified(profilesRoot, stagingDir, {"P1"}), 0);
        QCOMPARE(folderOf(readModlist("P1"), "id-pg"), QString("P1 - PGPatcher Output"));
    }

    void missingSource_createsFreshFolder() {
        QTemporaryDir tmp;
        profilesRoot = tmp.path() + "/profiles";
        stagingDir   = tmp.path() + "/mods";

        // Output mod whose on-disk folder does not exist -> fresh empty folder.
        ModList p1;
        p1.append(mkMod("id-skse", "SKSE Output", "SKSE Output", true));
        writeModlist("P1", p1);

        QCOMPARE(migrateOutputModsProfileQualified(profilesRoot, stagingDir, {"P1"}), 1);
        QCOMPARE(folderOf(readModlist("P1"), "id-skse"), QString("P1 - SKSE Output"));
        QVERIFY(QDir(stagingDir + "/P1 - SKSE Output/Data").exists());
    }
};

QTEST_MAIN(TestOutputModMigration)
#include "test_OutputModMigration.moc"
