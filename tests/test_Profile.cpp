#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include "core/ProfileManager.h"
#include "core/AppConfig.h"
#include "core/ModList.h"
#include "core/StagingFolder.h"
using namespace solero;

// Build a staging dir with one UUID folder per mod (each holding a marker file),
// and a profile whose modlist references those mods by id.
static ModEntry mkMod(const QString& id, const QString& name) {
    ModEntry e;
    e.type = EntryType::Mod;
    e.id = id;
    e.name = name;
    return e;
}

class TestProfile : public QObject {
    Q_OBJECT
private slots:
    void createProfile_dirExists() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("TestProfile");
        QVERIFY(QDir(tmp.path() + "/TestProfile").exists());
    }
    void listProfiles_returnsCreated() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("Alpha");
        mgr.createProfile("Beta");
        auto names = mgr.profileNames();
        QVERIFY(names.contains("Alpha"));
        QVERIFY(names.contains("Beta"));
    }
    void switchProfile_loadsCorrectData() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("P1");
        Profile* p = mgr.loadProfile("P1");
        QVERIFY(p != nullptr);
        QCOMPARE(p->name(), QString("P1"));
    }
    void deleteProfile_removesDir() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("ToDelete");
        QVERIFY(mgr.deleteProfile("ToDelete"));
        QVERIFY(!QDir(tmp.path() + "/ToDelete").exists());
    }
    void renameProfile_movesDir() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("OldName");
        QVERIFY(mgr.renameProfile("OldName", "NewName"));
        QVERIFY(!QDir(tmp.path() + "/OldName").exists());
        QVERIFY(QDir(tmp.path() + "/NewName").exists());
        QVERIFY(mgr.profileNames().contains("NewName"));
        QVERIFY(!mgr.profileNames().contains("OldName"));
    }
    void renameProfile_rejectsExistingTarget() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("A");
        mgr.createProfile("B");
        QVERIFY(!mgr.renameProfile("A", "B")); // target exists
        QVERIFY(QDir(tmp.path() + "/A").exists());
    }
    void renameProfile_rejectsInvalidName() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("A");
        QVERIFY(!mgr.renameProfile("A", ""));        // empty
        QVERIFY(!mgr.renameProfile("A", "a/b"));     // path separator
        QVERIFY(!mgr.renameProfile("A", "a\\b"));    // backslash
        QVERIFY(QDir(tmp.path() + "/A").exists());
    }
    void renameProfile_missingSourceFails() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        QVERIFY(!mgr.renameProfile("Ghost", "NewName"));
    }
    void fileRules_roundTrip() {
        QTemporaryDir tmp;
        {
            Profile p("Rules", tmp.path());
            p.setFileHidden("modA", "Data/x.dll", true);
            p.setFileHidden("modA", "Data/SKSE/y.esp", true);
            p.setWinnerOverride("Data/z.nif", "modB");
            QVERIFY(p.save());
            QVERIFY(QFile::exists(p.fileRulesPath()));
        }
        // Reload into a fresh Profile and confirm the rules survived.
        Profile p2("Rules", tmp.path());
        QVERIFY(p2.load());
        QVERIFY(p2.isFileHidden("modA", "Data/x.dll"));
        QVERIFY(p2.isFileHidden("modA", "Data/SKSE/y.esp"));
        QVERIFY(!p2.isFileHidden("modA", "Data/other.dll"));
        QCOMPARE(p2.winnerOverride("Data/z.nif"), QString("modB"));
        QVERIFY(p2.winnerOverride("Data/none.nif").isEmpty());

        // Unhide + clear override, save, reload - the rules are gone (sparse).
        p2.setFileHidden("modA", "Data/x.dll", false);
        p2.clearWinnerOverride("Data/z.nif");
        QVERIFY(p2.save());
        Profile p3("Rules", tmp.path());
        QVERIFY(p3.load());
        QVERIFY(!p3.isFileHidden("modA", "Data/x.dll"));
        QVERIFY(p3.isFileHidden("modA", "Data/SKSE/y.esp")); // the other one stays
        QVERIFY(p3.winnerOverride("Data/z.nif").isEmpty());
    }
    void fileRules_missingFileIsEmpty() {
        QTemporaryDir tmp;
        Profile p("NoRules", tmp.path());
        QVERIFY(p.load()); // no filerules.json -> backward compatible, no rules
        QVERIFY(!p.isFileHidden("anyMod", "Data/x.dll"));
        QVERIFY(p.winnerOverride("Data/x.dll").isEmpty());
    }

    // Staging-folder migration
    void migrate_renamesUuidFoldersToNames() {
        QTemporaryDir profiles, staging;
        AppConfig::instance().setStagingDir(staging.path());

        const QString id1 = "0032bcc7-aaaa";
        const QString id2 = "1142ddee-bbbb";
        // Create UUID folders on disk with a marker file inside each.
        QDir().mkpath(staging.path() + "/" + id1 + "/Data");
        QDir().mkpath(staging.path() + "/" + id2 + "/Data");
        QFile m1(staging.path() + "/" + id1 + "/Data/marker.esp"); m1.open(QIODevice::WriteOnly); m1.write("1"); m1.close();

        Profile p("Mig", profiles.path());
        p.modList().append(mkMod(id1, "SkyUI"));
        p.modList().append(mkMod(id2, "Cool/Mod:Bad")); // illegal chars
        QVERIFY(p.save());

        QVERIFY(p.migrateStagingFolders());

        // Folders renamed to sanitized names; UUID folders gone.
        QVERIFY(QDir(staging.path() + "/SkyUI").exists());
        QVERIFY(QFile::exists(staging.path() + "/SkyUI/Data/marker.esp"));
        QVERIFY(!QDir(staging.path() + "/" + id1).exists());
        QVERIFY(QDir(staging.path() + "/Cool_Mod_Bad").exists());
        QVERIFY(!QDir(staging.path() + "/" + id2).exists());

        // stagingFolder fields backfilled.
        QCOMPARE(p.stagingFolderFor(id1), QString("SkyUI"));
        QCOMPARE(p.stagingFolderFor(id2), QString("Cool_Mod_Bad"));

        // Mapping + backup written.
        QVERIFY(QFile::exists(profiles.path() + "/Mig/staging-folder-migration.json"));
        QVERIFY(QFile::exists(profiles.path() + "/Mig/modlist.json.bak-stagingmigration"));
        QFile mf(profiles.path() + "/Mig/staging-folder-migration.json");
        QVERIFY(mf.open(QIODevice::ReadOnly));
        auto obj = QJsonDocument::fromJson(mf.readAll()).object();
        QCOMPARE(obj[id1].toString(), QString("SkyUI"));
        QCOMPARE(obj[id2].toString(), QString("Cool_Mod_Bad"));
    }

    void migrate_sameNameGetsSuffix() {
        QTemporaryDir profiles, staging;
        AppConfig::instance().setStagingDir(staging.path());
        const QString id1 = "uuid-aaaa", id2 = "uuid-bbbb";
        QDir().mkpath(staging.path() + "/" + id1);
        QDir().mkpath(staging.path() + "/" + id2);

        Profile p("Dup", profiles.path());
        p.modList().append(mkMod(id1, "My Mod"));
        p.modList().append(mkMod(id2, "My Mod"));
        QVERIFY(p.save());
        QVERIFY(p.migrateStagingFolders());

        QCOMPARE(p.stagingFolderFor(id1), QString("My Mod"));
        QCOMPARE(p.stagingFolderFor(id2), QString("My Mod (2)"));
        QVERIFY(QDir(staging.path() + "/My Mod").exists());
        QVERIFY(QDir(staging.path() + "/My Mod (2)").exists());
    }

    void migrate_idempotentSecondRunIsNoOp() {
        QTemporaryDir profiles, staging;
        AppConfig::instance().setStagingDir(staging.path());
        const QString id1 = "uuid-cccc";
        QDir().mkpath(staging.path() + "/" + id1);

        Profile p("Idem", profiles.path());
        p.modList().append(mkMod(id1, "Some Mod"));
        QVERIFY(p.save());
        QVERIFY(p.migrateStagingFolders());      // first run: changed
        QVERIFY(!p.migrateStagingFolders());     // second run: no-op
        QCOMPARE(p.stagingFolderFor(id1), QString("Some Mod"));
    }

    void migrate_missingFolderJustRecordsName() {
        QTemporaryDir profiles, staging;
        AppConfig::instance().setStagingDir(staging.path());
        const QString id1 = "uuid-dddd"; // no folder on disk

        Profile p("Missing", profiles.path());
        p.modList().append(mkMod(id1, "Ghost Mod"));
        QVERIFY(p.save());
        QVERIFY(p.migrateStagingFolders());
        QCOMPARE(p.stagingFolderFor(id1), QString("Ghost Mod"));
        // No rename happened -> no backup file created.
        QVERIFY(!QFile::exists(profiles.path() + "/Missing/modlist.json.bak-stagingmigration"));
    }

    // The managed shader cache round-trips as first-class Profile state.
    void shaderCache_roundTrip() {
        QTemporaryDir profiles, staging;
        AppConfig::instance().setStagingDir(staging.path());
        {
            Profile p("RT", profiles.path());
            p.shaderCache().managed       = true;
            p.shaderCache().stagingFolder = "MyCacheFolder";
            QVERIFY(p.save());
            QVERIFY(QFile::exists(p.shaderCachePath()));
        }
        Profile p2("RT", profiles.path());
        QVERIFY(p2.load());
        QVERIFY(p2.shaderCache().managed);
        QVERIFY(p2.shaderCache().active());
        QCOMPARE(p2.shaderCache().stagingFolder, QString("MyCacheFolder"));
    }

    // A legacy profile stored the cache as a hidden isManagedCache mod-list entry;
    // load() lifts it into shaderCache state and drops it from the list.
    void shaderCache_migratesLegacyEntry() {
        QTemporaryDir profiles, staging;
        AppConfig::instance().setStagingDir(staging.path());
        const QString dir = profiles.path() + "/Legacy";
        QVERIFY(QDir().mkpath(dir));
        const QByteArray legacy = R"([
          {"type":"mod","id":"m0","name":"Mod0","enabled":true,"stagingFolder":"Mod0"},
          {"type":"mod","id":"cache","name":"CS Cache","enabled":true,
           "isManagedCache":true,"stagingFolder":"CSCacheFolder"}
        ])";
        QFile f(dir + "/modlist.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(legacy); f.close();

        Profile p("Legacy", profiles.path());
        QVERIFY(p.load());
        // Cache entry lifted out of the list…
        QVERIFY(p.modList().findById("cache") == nullptr);
        QCOMPARE(p.modList().count(), 1);
        // …into shaderCache state.
        QVERIFY(p.shaderCache().managed);
        QCOMPARE(p.shaderCache().stagingFolder, QString("CSCacheFolder"));
        QVERIFY(QFile::exists(p.shaderCachePath()));

        // Persisted: a fresh reload still has the cache out of the list.
        Profile p2("Legacy", profiles.path());
        QVERIFY(p2.load());
        QVERIFY(p2.modList().findById("cache") == nullptr);
        QVERIFY(p2.shaderCache().managed);
        QCOMPARE(p2.shaderCache().stagingFolder, QString("CSCacheFolder"));
    }
};
QTEST_MAIN(TestProfile)
#include "test_Profile.moc"
