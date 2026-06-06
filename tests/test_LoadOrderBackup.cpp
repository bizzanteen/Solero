#include <QtTest>
#include <QTemporaryDir>
#include "core/Profile.h"
#include "core/PluginList.h"
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
};
QTEST_MAIN(TestLoadOrderBackup)
#include "test_LoadOrderBackup.moc"
