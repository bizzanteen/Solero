// tests/test_ProfileManifest.cpp
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "io/ProfileManifest.h"
#include "core/Profile.h"
#include "core/ProfileManager.h"
#include "core/ModList.h"
#include "core/PluginList.h"
#include "core/Types.h"
using namespace solero;

static void write(const QString& p, const QString& c) {
    QDir().mkpath(QFileInfo(p).path());
    QFile f(p); f.open(QIODevice::WriteOnly); f.write(c.toUtf8());
}

// Build a profile with: a separator, a parent+child group, a FOMOD mod, and a
// plugin load order. `fomodDir` gets the FOMOD mod's choices log written for it.
static Profile makeProfile(const QString& root, const QString& fomodDir) {
    Profile p("SharedList", root);

    ModEntry sep; sep.type = EntryType::Separator; sep.name = "Combat";
    sep.id = "sep-1"; sep.color = "#aa0000"; sep.icon = ":/icons/sep/combat.svg";
    p.modList().append(sep);

    ModEntry parent; parent.type = EntryType::Mod; parent.name = "Parent Mod";
    parent.id = "P"; parent.nexusModId = "100"; parent.nexusFileId = "200";
    parent.version = "1.2.0"; parent.tags = {"gameplay"};
    p.modList().append(parent);

    ModEntry child; child.type = EntryType::Mod; child.name = "Child Mod";
    child.id = "C"; child.parentId = "P"; child.enabled = false;
    p.modList().append(child);

    ModEntry fomod; fomod.type = EntryType::Mod; fomod.name = "FOMOD Mod";
    fomod.id = "F"; fomod.isFomod = true; fomod.hasFomodChoices = true;
    p.modList().append(fomod);

    PluginEntry a; a.filename = "Skyrim.esm"; a.enabled = true;  p.pluginList().append(a);
    PluginEntry b; b.filename = "Mod.esp";    b.enabled = true;  p.pluginList().append(b);
    PluginEntry c; c.filename = "Off.esp";    c.enabled = false; p.pluginList().append(c);

    write(fomodDir + "/F.json",
          "{ \"installer_version\":\"1.0\", \"steps\":[ "
          "{ \"step\":\"Step 1\", \"selected\":[\"OptA\",\"OptB\"] } ] }");
    return p;
}

class TestProfileManifest : public QObject { Q_OBJECT
private slots:

    // EXPORT round-trips order, enabled, separator, group relationship (as a
    // portable ordinal parentIndex), plugin order, and FOMOD choices.
    void exportRoundTrips() {
        QTemporaryDir tmp;
        const QString fomodDir = tmp.path() + "/fomod";
        Profile p = makeProfile(tmp.path() + "/profiles", fomodDir);

        const QString path = tmp.path() + "/SharedList.solero-profile.json";
        QVERIFY(ProfileManifest::exportToFile(p, path, fomodDir));

        QFile f(path); QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

        QCOMPARE(root["format"].toString(), QString("solero-profile/1"));
        QCOMPARE(root["exportedProfile"].toString(), QString("SharedList"));

        const QJsonArray mods = root["mods"].toArray();
        QCOMPARE(mods.size(), 4);

        // [0] separator
        QCOMPARE(mods[0].toObject()["type"].toString(), QString("separator"));
        QCOMPARE(mods[0].toObject()["name"].toString(), QString("Combat"));
        QCOMPARE(mods[0].toObject()["color"].toString(), QString("#aa0000"));

        // [1] parent mod (nexus ids + tags)
        const QJsonObject parent = mods[1].toObject();
        QCOMPARE(parent["name"].toString(), QString("Parent Mod"));
        QCOMPARE(parent["nexusModId"].toString(), QString("100"));
        QCOMPARE(parent["nexusFileId"].toString(), QString("200"));
        QCOMPARE(parent["tags"].toArray().size(), 1);

        // [2] child mod -> parentIndex is the ORDINAL of the parent (1), enabled false
        const QJsonObject child = mods[2].toObject();
        QCOMPARE(child["name"].toString(), QString("Child Mod"));
        QVERIFY(child.contains("parentIndex"));
        QCOMPARE(child["parentIndex"].toInt(), 1);
        QCOMPARE(child["enabled"].toBool(true), false);

        // [3] FOMOD mod -> fomodChoices steps survived
        const QJsonArray steps = mods[3].toObject()["fomodChoices"].toArray();
        QCOMPARE(steps.size(), 1);
        QCOMPARE(steps[0].toObject()["step"].toString(), QString("Step 1"));
        QCOMPARE(steps[0].toObject()["selected"].toArray().size(), 2);

        // plugin order + enabled flags
        const QJsonArray plugins = root["pluginOrder"].toArray();
        QCOMPARE(plugins.size(), 3);
        QCOMPARE(plugins[0].toObject()["name"].toString(), QString("Skyrim.esm"));
        QCOMPARE(plugins[0].toObject()["enabled"].toBool(false), true);
        QCOMPARE(plugins[2].toObject()["name"].toString(), QString("Off.esp"));
        QCOMPARE(plugins[2].toObject()["enabled"].toBool(true), false);
    }

    // BUILD matches against an in-memory pool: all three present -> matched,
    // group + plugin order reconstructed, ids reused, fomod choices collected.
    void buildMatchesPool() {
        QTemporaryDir tmp;
        const QString fomodDir = tmp.path() + "/fomod";
        Profile p = makeProfile(tmp.path() + "/profiles", fomodDir);
        const QJsonDocument manifest = ProfileManifest::toJson(p, fomodDir);

        // Pool with DIFFERENT ids than the manifest's (P/C/F) - match by nexus/name.
        ModList pool;
        ModEntry parent; parent.type = EntryType::Mod; parent.name = "Parent Mod";
        parent.id = "pool-parent"; parent.nexusModId = "100"; parent.nexusFileId = "200";
        parent.sourceArchive = "/archives/parent.7z"; pool.append(parent);
        ModEntry child; child.type = EntryType::Mod; child.name = "Child Mod";
        child.id = "pool-child"; pool.append(child);
        ModEntry fomod; fomod.type = EntryType::Mod; fomod.name = "FOMOD Mod";
        fomod.id = "pool-fomod"; pool.append(fomod);
        ModEntry extra; extra.type = EntryType::Mod; extra.name = "Unrelated";
        extra.id = "pool-extra"; pool.append(extra);

        ManifestBuildResult b = ProfileManifest::build(manifest, pool);
        QCOMPARE(b.exportedProfile, QString("SharedList"));
        QCOMPARE(b.modsMatched, 3);
        QCOMPARE(b.separators, 1);
        QCOMPARE(b.missing.size(), 0);
        QCOMPARE(b.modList.count(), 4);

        // ids were reused from the pool (so staging folders resolve).
        const ModEntry* bp = b.modList.findById("pool-parent");
        const ModEntry* bc = b.modList.findById("pool-child");
        QVERIFY(bp); QVERIFY(bc);
        QCOMPARE(bp->sourceArchive, QString("/archives/parent.7z")); // staging meta kept

        // group relationship reconstructed via the portable ordinal: child.parentId
        // points at the matched parent, and they are contiguous (sep, parent, child).
        QCOMPARE(bc->parentId, QString("pool-parent"));
        QCOMPARE(b.modList.at(1).id, QString("pool-parent"));
        QCOMPARE(b.modList.at(2).id, QString("pool-child"));

        // enabled flag carried from the manifest (child was disabled).
        QCOMPARE(bc->enabled, false);

        // FOMOD choices collected under the MATCHED (pool) id.
        QVERIFY(b.fomodChoices.contains("pool-fomod"));
        QCOMPARE(b.fomodChoices.value("pool-fomod").size(), 1);
        QVERIFY(b.modList.findById("pool-fomod")->hasFomodChoices);

        // plugin order reconstructed with flags + enabled state.
        QCOMPARE(b.pluginList.count(), 3);
        QCOMPARE(b.pluginList.at(0).filename, QString("Skyrim.esm"));
        QVERIFY(b.pluginList.at(0).isMaster);
        QCOMPARE(b.pluginList.at(2).enabled, false);
    }

    // BUILD with a partial pool: the missing mod is reported (and its child loses
    // the parent link), not created as a placeholder.
    void buildReportsMissing() {
        QTemporaryDir tmp;
        const QString fomodDir = tmp.path() + "/fomod";
        Profile p = makeProfile(tmp.path() + "/profiles", fomodDir);
        const QJsonDocument manifest = ProfileManifest::toJson(p, fomodDir);

        // Pool is MISSING the parent mod (no nexus 100, no "Parent Mod" name).
        ModList pool;
        ModEntry child; child.type = EntryType::Mod; child.name = "Child Mod";
        child.id = "pool-child"; pool.append(child);
        ModEntry fomod; fomod.type = EntryType::Mod; fomod.name = "FOMOD Mod";
        fomod.id = "pool-fomod"; pool.append(fomod);

        ManifestBuildResult b = ProfileManifest::build(manifest, pool);
        QCOMPARE(b.modsMatched, 2);
        QCOMPARE(b.separators, 1);
        QCOMPARE(b.missing.size(), 1);
        QCOMPARE(b.missing[0].name, QString("Parent Mod"));
        QCOMPARE(b.missing[0].nexusModId, QString("100"));
        QCOMPARE(b.missing[0].version, QString("1.2.0"));
        // built list: separator + child + fomod (parent skipped, not a placeholder).
        QCOMPARE(b.modList.count(), 3);
        // child's parent was skipped -> it is now top-level.
        QCOMPARE(b.modList.findById("pool-child")->parentId, QString());
    }

    // End-to-end import: creates a disambiguated profile, persists it, and
    // back-fills fomod-choices.json under the matched mod id.
    void importFileCreatesProfile() {
        QTemporaryDir tmp;
        const QString srcFomod = tmp.path() + "/srcfomod";
        Profile p = makeProfile(tmp.path() + "/src", srcFomod);
        const QString manifestPath = tmp.path() + "/SharedList.solero-profile.json";
        QVERIFY(ProfileManifest::exportToFile(p, manifestPath, srcFomod));

        ModList pool;
        ModEntry parent; parent.type = EntryType::Mod; parent.name = "Parent Mod";
        parent.id = "pool-parent"; parent.nexusModId = "100"; parent.nexusFileId = "200";
        pool.append(parent);
        ModEntry child; child.type = EntryType::Mod; child.name = "Child Mod";
        child.id = "pool-child"; pool.append(child);
        ModEntry fomod; fomod.type = EntryType::Mod; fomod.name = "FOMOD Mod";
        fomod.id = "pool-fomod"; pool.append(fomod);

        const QString profilesRoot = tmp.path() + "/profiles";
        const QString destFomod    = tmp.path() + "/destfomod";
        ProfileManager pm(profilesRoot);

        auto r = ProfileManifest::importFile(manifestPath, pm, pool, destFomod);
        QVERIFY2(r.success, r.errorMessage.toUtf8());
        QCOMPARE(r.profileName, QString("SharedList"));
        QCOMPARE(r.modsMatched, 3);
        QCOMPARE(r.separators, 1);
        QCOMPARE(r.missing.size(), 0);

        // Profile persisted with all entries + plugins.
        Profile* loaded = pm.loadProfile("SharedList");
        QVERIFY(loaded);
        QCOMPARE(loaded->modList().count(), 4);
        QCOMPARE(loaded->pluginList().count(), 3);

        // fomod-choices.json written under the matched (pool) id.
        QVERIFY(QFile::exists(destFomod + "/pool-fomod.json"));
        QFile cf(destFomod + "/pool-fomod.json"); QVERIFY(cf.open(QIODevice::ReadOnly));
        const QJsonObject co = QJsonDocument::fromJson(cf.readAll()).object();
        QCOMPARE(co["steps"].toArray().size(), 1);

        // Re-import collides -> disambiguated name.
        auto r2 = ProfileManifest::importFile(manifestPath, pm, pool, destFomod);
        QVERIFY(r2.success);
        QCOMPARE(r2.profileName, QString("SharedList (imported)"));
    }
};

QTEST_MAIN(TestProfileManifest)
#include "test_ProfileManifest.moc"
