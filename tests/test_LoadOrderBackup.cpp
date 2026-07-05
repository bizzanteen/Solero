#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "core/Profile.h"
#include "core/PluginList.h"
#include "core/ModList.h"
#include "core/LoadOrderBackup.h"
using namespace solero;

// Build a plugin entry with the right band flags inferred from the extension.
static PluginEntry mk(const QString& name, bool enabled, bool official = false) {
    PluginEntry p;
    p.filename   = name;
    p.enabled    = enabled;
    p.isMaster   = name.endsWith(".esm", Qt::CaseInsensitive);
    p.isLight    = name.endsWith(".esl", Qt::CaseInsensitive);
    p.isOfficial = official;
    return p;
}

// Build a mod-list entry (Mod or Separator) with a fixed id for snapshot checks.
static ModEntry mkMod(const QString& id, bool enabled, EntryType t = EntryType::Mod) {
    ModEntry e;
    e.id      = id;
    e.type    = t;
    e.enabled = enabled;
    e.name    = id;
    return e;
}

class TestLoadOrderBackup : public QObject {
    Q_OBJECT
private slots:
    // Snapshot -> mutate -> restore reproduces the snapshot's order + enabled for
    // shared plugins; new plugins drop to the bottom; removed ones are ignored.
    void backupAndRestore_roundtrips() {
        QTemporaryDir tmp;
        Profile profile("P", tmp.path());
        PluginList& pl = profile.pluginList();
        pl.append(mk("Skyrim.esm", true, /*official=*/true));
        pl.append(mk("A.esp", true));
        pl.append(mk("B.esp", false));
        pl.append(mk("C.esp", true));

        // Snapshot the current state.
        const QString path = LoadOrderBackup::create(profile, "snap");
        QVERIFY(!path.isEmpty());

        // It shows up in the listing (newest first) with the right count.
        auto backups = LoadOrderBackup::list(profile);
        QCOMPARE(backups.size(), 1);
        QCOMPARE(backups.first().label, QString("snap"));
        QCOMPARE(backups.first().pluginCount, 4);

        // Mutate: reorder (C before A before B), toggle states, drop C(*),
        // remove B (gone since the snapshot), and add a brand-new plugin.
        PluginList& live = profile.pluginList();
        // Rebuild the live list: officials kept, order shuffled, B removed, new added.
        live = PluginList(); // reset
        live.append(mk("Skyrim.esm", true, /*official=*/true));
        live.append(mk("C.esp", false)); // was enabled in snapshot
        live.append(mk("A.esp", false)); // was enabled in snapshot
        live.append(mk("NEW.esp", true)); // added after the snapshot

        const auto snap = LoadOrderBackup::load(path);
        QVERIFY(snap.valid);
        live.restoreSnapshot(snap.plugins);

        // Official stays pinned to the top.
        QCOMPARE(live.at(0).filename, QString("Skyrim.esm"));
        // Shared plugins restored to the snapshot's relative order (A before C)
        // and the snapshot's enabled state (A on, C on).
        QCOMPARE(live.at(1).filename, QString("A.esp"));
        QCOMPARE(live.at(1).enabled, true);
        QCOMPARE(live.at(2).filename, QString("C.esp"));
        QCOMPARE(live.at(2).enabled, true);
        // The plugin added since the snapshot drops to the bottom, keeping its
        // current enabled state.
        QCOMPARE(live.at(3).filename, QString("NEW.esp"));
        QCOMPARE(live.at(3).enabled, true);
        // B (removed since the snapshot) is gracefully ignored.
        QCOMPARE(live.count(), 4);
    }

    // Restore must keep bands valid: masters/light masters above regular plugins
    // even if the snapshot order would interleave them.
    void restore_preservesBands() {
        PluginList live;
        live.append(mk("Reg.esp", true));
        live.append(mk("Lib.esm", true));
        live.append(mk("Light.esl", true));

        // Snapshot order deliberately puts the esp first, then esm, then esl.
        QList<QPair<QString, bool>> snap = {
            {"Reg.esp", false}, {"Lib.esm", true}, {"Light.esl", false}
        };
        live.restoreSnapshot(snap);

        // Result must be banded: master, light, esp - regardless of snapshot order.
        QCOMPARE(live.at(0).filename, QString("Lib.esm"));
        QCOMPARE(live.at(1).filename, QString("Light.esl"));
        QCOMPARE(live.at(2).filename, QString("Reg.esp"));
        // Enabled states still come from the snapshot.
        QCOMPARE(live.at(2).enabled, false);
    }

    // No lo-backups dir -> list() is empty (backward compatible).
    void list_missingDir_empty() {
        QTemporaryDir tmp;
        Profile profile("Fresh", tmp.path());
        QVERIFY(LoadOrderBackup::list(profile).isEmpty());
    }

    // a snapshot also captures the ordered mod list (id + enabled +
    // separator flag), and load() reproduces it in order.
    void modList_roundtrips() {
        QTemporaryDir tmp;
        Profile profile("P", tmp.path());
        profile.pluginList().append(mk("Skyrim.esm", true, /*official=*/true));

        ModList& ml = profile.modList();
        ml.append(mkMod("sep1", false, EntryType::Separator));
        ml.append(mkMod("modA", true));
        ml.append(mkMod("modB", false));

        const QString path = LoadOrderBackup::create(profile, "with-mods");
        QVERIFY(!path.isEmpty());

        // The listing reports the mod count for a new-format backup.
        auto backups = LoadOrderBackup::list(profile);
        QCOMPARE(backups.size(), 1);
        QCOMPARE(backups.first().modCount, 3);

        const auto snap = LoadOrderBackup::load(path);
        QVERIFY(snap.valid);
        QVERIFY(snap.hasMods);
        QCOMPARE(snap.mods.size(), 3);
        QCOMPARE(snap.mods.at(0).id, QString("sep1"));
        QVERIFY(snap.mods.at(0).isSeparator);
        QCOMPARE(snap.mods.at(1).id, QString("modA"));
        QCOMPARE(snap.mods.at(1).enabled, true);
        QCOMPARE(snap.mods.at(1).isSeparator, false);
        QCOMPARE(snap.mods.at(2).id, QString("modB"));
        QCOMPARE(snap.mods.at(2).enabled, false);

        // The mod order round-trips through ModList::setOrder (as the restore does).
        QStringList order;
        for (const auto& e : snap.mods) order << e.id;
        // Shuffle the live list, then restore.
        ml.setOrder({"modB", "modA", "sep1"});
        QVERIFY(ml.setOrder(order));
        QCOMPARE(ml.at(0).id, QString("sep1"));
        QCOMPARE(ml.at(1).id, QString("modA"));
        QCOMPARE(ml.at(2).id, QString("modB"));
    }

    // back-compat: an old plugin-only snapshot (no "mods" section) still
    // loads - hasMods is false and the mod list is left untouched by callers.
    void oldFormat_pluginOnly_backCompat() {
        QTemporaryDir tmp;
        const QString path = tmp.path() + "/lo-old.json";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"label":"legacy","created":"2020-01-01T00:00:00",
                    "plugins":[{"name":"A.esp","enabled":true},
                               {"name":"B.esp","enabled":false}]})");
        f.close();

        const auto snap = LoadOrderBackup::load(path);
        QVERIFY(snap.valid);
        QVERIFY(!snap.hasMods);          // no mod-list section
        QVERIFY(snap.mods.isEmpty());
        QCOMPARE(snap.plugins.size(), 2); // plugins still restore
        QCOMPARE(snap.plugins.at(0).first, QString("A.esp"));
        QCOMPARE(snap.plugins.at(1).second, false);
    }
};
QTEST_MAIN(TestLoadOrderBackup)
#include "test_LoadOrderBackup.moc"
