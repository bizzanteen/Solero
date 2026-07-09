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

static QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

class TestProfile : public QObject {
    Q_OBJECT
private slots:
    // per-profile local saves flag round-trips through settings.json.
    void localSaves_roundTrips() {
        QTemporaryDir tmp;
        Profile p("Char A", tmp.path() + "/profiles");
        QVERIFY(!p.localSaves()); // default off
        p.setLocalSaves(true);
        QVERIFY(p.save());

        Profile reloaded("Char A", tmp.path() + "/profiles");
        QVERIFY(reloaded.load());
        QVERIFY(reloaded.localSaves());
    }

    // The per-profile INI flag round-trips through settings.json.
    void localInis_roundTrips() {
        QTemporaryDir tmp;
        Profile p("P", tmp.path() + "/profiles");
        QVERIFY(!p.localInis()); // default off for a fresh profile
        p.setLocalInis(true);
        QVERIFY(p.saveSettings());

        Profile reloaded("P", tmp.path() + "/profiles");
        QVERIFY(reloaded.load());
        QVERIFY(reloaded.localInis());
    }

    // Migration: a profile from before the flag existed (no localInis key in
    // settings.json) defaults localInis to ON when it already carries INI files, so
    // the old presence-based deploy behaviour is preserved.
    void localInis_migratesWhenIniPresent() {
        QTemporaryDir tmp;
        const QString root = tmp.path() + "/profiles";
        Profile seed("Legacy", root);
        QVERIFY(seed.save());
        // Give it an INI but write a settings.json without the localInis key.
        { QFile f(seed.skyrimIniPath()); QDir().mkpath(QFileInfo(f).path());
          f.open(QIODevice::WriteOnly); f.write("[Display]\niSize W=1\n"); }
        { QFile f(seed.profileSettingsPath());
          f.open(QIODevice::WriteOnly); f.write("{ \"localSaves\": false }"); }

        Profile migrated("Legacy", root);
        QVERIFY(migrated.load());
        QVERIFY(migrated.hasProfileInis());
        QVERIFY(migrated.localInis()); // migrated to on because it has INIs

        // A profile with no INIs and no flag stays off.
        Profile fresh("Empty", root);
        QVERIFY(fresh.save());
        { QFile f(fresh.profileSettingsPath());
          f.open(QIODevice::WriteOnly); f.write("{ \"localSaves\": false }"); }
        Profile freshLoaded("Empty", root);
        QVERIFY(freshLoaded.load());
        QVERIFY(!freshLoaded.localInis());
    }

    // The save subfolder name is a filesystem/Skyrim-safe version of the profile name.
    void saveFolderName_sanitizes() {
        QCOMPARE(Profile::sanitizeSaveFolder("Char A"), QString("Char A"));
        QCOMPARE(Profile::sanitizeSaveFolder("A/B:C*?"), QString("A_B_C__"));
        QCOMPARE(Profile::sanitizeSaveFolder("  trimmed  "), QString("trimmed"));
        QVERIFY(!Profile::sanitizeSaveFolder("").isEmpty()); // never empty
    }

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

    // Loading a profile that still has the old separate managed cache retires it:
    // shadercache.json is deleted and any hidden isManagedCache mod-list entry is
    // dropped (the cache now lives inside the CS mod). Migration is idempotent.
    void shaderCache_migratesAwayFromManagedCache() {
        QTemporaryDir profiles, staging;
        AppConfig::instance().setStagingDir(staging.path());
        const QString dir = profiles.path() + "/Legacy";
        QVERIFY(QDir().mkpath(dir));

        // A hidden isManagedCache entry + a normal mod.
        const QByteArray modlist = R"([
          {"type":"mod","id":"m0","name":"Mod0","enabled":true,"stagingFolder":"Mod0"},
          {"type":"mod","id":"cache","name":"CS Cache","enabled":true,
           "isManagedCache":true,"stagingFolder":"CSCacheFolder"}
        ])";
        QFile ml(dir + "/modlist.json");
        QVERIFY(ml.open(QIODevice::WriteOnly));
        ml.write(modlist); ml.close();

        // A legacy shadercache.json naming a separate cache staging folder.
        const QByteArray sc = R"({"managed":true,"folders":{"f9":"X - Shader Cache"}})";
        QFile scf(dir + "/shadercache.json");
        QVERIFY(scf.open(QIODevice::WriteOnly));
        scf.write(sc); scf.close();

        // Those old folders exist on disk in staging (to be retired to Trash).
        QVERIFY(QDir().mkpath(staging.path() + "/X - Shader Cache/Data/ShaderCache"));
        QVERIFY(QDir().mkpath(staging.path() + "/CSCacheFolder/Data"));

        Profile p("Legacy", profiles.path());
        QVERIFY(p.load());
        // The hidden cache entry is dropped from the list; the real mod stays.
        QVERIFY(p.modList().findById("cache") == nullptr);
        QVERIFY(p.modList().findById("m0") != nullptr);
        QCOMPARE(p.modList().count(), 1);
        // shadercache.json is gone (migration deletes it after retiring the folders).
        QVERIFY(!QFile::exists(p.shaderCachePath()));

        // Idempotent: a fresh load finds nothing to migrate and leaves no json behind.
        Profile p2("Legacy", profiles.path());
        QVERIFY(p2.load());
        QVERIFY(p2.modList().findById("cache") == nullptr);
        QVERIFY(!QFile::exists(p2.shaderCachePath()));
    }

    // Executables: round-trip + per-profile seeding
    void executables_roundTrip() {
        QTemporaryDir profiles;
        {
            Profile p("Exe", profiles.path());
            Executable a;
            a.id = "loot"; a.name = "LOOT"; a.binaryPath = "/bin/loot";
            a.arguments = "--game skyrimse"; a.runtime = RuntimeType::Native;
            Executable b;
            b.id = "radium"; b.name = "Radium"; b.binaryPath = "/bin/radium";
            b.runtime = RuntimeType::Proton; b.isCapturingOutput = true;
            b.outputModId = "out-123"; b.writesOutputDirectly = true;
            p.executables() << a << b;
            QVERIFY(p.save());
            QVERIFY(QFile::exists(p.executablesPath()));
        }
        Profile p2("Exe", profiles.path());
        QVERIFY(p2.load());
        QCOMPARE(p2.executables().size(), 2);
        QCOMPARE(p2.executables()[0].name, QString("LOOT"));
        QCOMPARE(p2.executables()[0].arguments, QString("--game skyrimse"));
        QCOMPARE(p2.executables()[1].name, QString("Radium"));
        QCOMPARE(p2.executables()[1].runtime, RuntimeType::Proton);
        QVERIFY(p2.executables()[1].isCapturingOutput);
        QCOMPARE(p2.executables()[1].outputModId, QString("out-123"));
    }

    // Seeding copies the template only when the profile has no executables yet,
    // and re-resolves each tool's outputModId through the caller's resolver.
    void seedExecutables_seedsOnlyWhenEmpty() {
        QTemporaryDir profiles;
        Executable tmpl;
        tmpl.id = "radium"; tmpl.name = "Radium"; tmpl.binaryPath = "/bin/radium";
        tmpl.outputModId = "TEMPLATE_ID";
        const QList<Executable> templateTools{ tmpl };
        auto resolver = [](const Executable&) { return QString("PROFILE_LOCAL_ID"); };

        Profile p("Seed", profiles.path());
        QVERIFY(p.executables().isEmpty());
        QVERIFY(p.seedExecutablesFrom(templateTools, resolver)); // empty -> seeds
        QCOMPARE(p.executables().size(), 1);
        QVERIFY(QFile::exists(p.executablesPath()));

        // Calling again (now non-empty) is a no-op and does not duplicate.
        QVERIFY(!p.seedExecutablesFrom(templateTools, resolver));
        QCOMPARE(p.executables().size(), 1);

        // Seeding is gated only by the in-memory list, not by the file on disk:
        // a fresh Profile with an (even empty) executables.json present but an
        // empty in-memory list still seeds. One-time-ness is enforced at the app
        // level (toolsMigratedToPerProfile), not here.
        Profile p2("Seed", profiles.path());
        QVERIFY(QFile::exists(p2.executablesPath())); // p's save left a file behind
        QVERIFY(p2.executables().isEmpty());
        QVERIFY(p2.seedExecutablesFrom(templateTools, resolver)); // proceeds anyway
        QCOMPARE(p2.executables().size(), 1);
    }

    // The seeded tool's outputModId is re-resolved for this profile, never the
    // template's id.
    void seedExecutables_reResolvesOutputModPerProfile() {
        QTemporaryDir profiles;
        Executable tmpl;
        tmpl.id = "radium"; tmpl.name = "Radium"; tmpl.binaryPath = "/bin/radium";
        tmpl.outputModId = "TEMPLATE_ID";
        // An extra action also carrying a template output id - must be cleared.
        ToolAction act; act.label = "Run TexGen"; act.outputModId = "TEMPLATE_ACT_ID";
        tmpl.extraActions << act;

        Profile p("Resolve", profiles.path());
        QVERIFY(p.seedExecutablesFrom({ tmpl },
            [](const Executable&) { return QString("PROFILE_LOCAL_ID"); }));
        QCOMPARE(p.executables().size(), 1);
        QCOMPARE(p.executables()[0].outputModId, QString("PROFILE_LOCAL_ID"));
        QCOMPARE(p.executables()[0].extraActions.size(), 1);
        QVERIFY(p.executables()[0].extraActions[0].outputModId.isEmpty());
    }

    void saveModListOnly_writesModlistLeavesOthersUntouched() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("P");
        Profile* p = mgr.loadProfile("P");
        QVERIFY(p);
        p->modList().append(mkMod("m0", "Mod0"));
        p->modList().append(mkMod("m1", "Mod1"));
        p->modList().append(mkMod("m2", "Mod2"));
        QVERIFY(p->save()); // writes modlist.json + the sibling files

        // The sibling files a reorder must not rewrite. (shadercache.json is absent
        // for an unmanaged cache - save() removes it - so it isn't in this set.)
        const QStringList siblings = {
            p->pluginsPath(), p->executablesPath(),
            p->fileRulesPath(), p->loadOrderStatePath()
        };
        for (const QString& f : siblings) QVERIFY2(QFile::exists(f), qPrintable(f));

        // Delete the siblings, then reorder + saveModListOnly(). Only modlist.json
        // should be (re)written; the deleted siblings must stay gone.
        for (const QString& f : siblings) QVERIFY(QFile::remove(f));
        const QByteArray before = readFile(p->modlistPath());
        p->modList().move(0, 2); // m0,m1,m2 -> m1,m2,m0
        QVERIFY(p->saveModListOnly());

        const QByteArray after = readFile(p->modlistPath());
        QVERIFY(after != before);            // modlist.json rewritten with new order
        QVERIFY(after.contains("\"m1\""));   // new order persisted
        for (const QString& f : siblings)
            QVERIFY2(!QFile::exists(f), qPrintable(f)); // untouched (still absent)
    }

    void matchOutputModId_caseInsensitiveAndMisses() {
        ModList ml;
        ModEntry out = mkMod("out-id", "Radium Output");
        out.isOutputMod = true;
        ml.append(mkMod("plain", "Some Mod")); // not an output mod
        ml.append(out);
        // Case-insensitive name match returns the output mod's id.
        QCOMPARE(Profile::matchOutputModId(ml, "radium output"), QString("out-id"));
        QCOMPARE(Profile::matchOutputModId(ml, "RADIUM OUTPUT"), QString("out-id"));
        // Unknown name, and a non-output same-named mod, both miss.
        QVERIFY(Profile::matchOutputModId(ml, "Nope").isEmpty());
        QVERIFY(Profile::matchOutputModId(ml, "Some Mod").isEmpty());
        QVERIFY(Profile::matchOutputModId(ml, "").isEmpty());
    }
};
QTEST_MAIN(TestProfile)
#include "test_Profile.moc"
