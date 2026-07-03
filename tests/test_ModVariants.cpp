#include <QtTest>
#include <QTemporaryDir>
#include "core/ModList.h"
using namespace solero;

static ModEntry mod(const QString& id, const QString& ver, const QString& fid,
                    const QString& folder) {
    ModEntry e; e.type = EntryType::Mod; e.id = id; e.name = "M" + id;
    e.version = ver; e.nexusModId = "111"; e.nexusFileId = fid;
    e.stagingFolder = folder; e.sourceArchive = "/dl/" + id + ".zip";
    return e;
}

class TestModVariants : public QObject {
    Q_OBJECT
private slots:
    void keepBothSnapshotsCurrentAsVariantZero() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        ModVariant v{"2.0", "f2", "M (2.0)", "/dl/a2.zip", false};
        QVERIFY(ml.keepBothAddVariant("a", v));
        const ModEntry* e = ml.findById("a");
        QCOMPARE(e->variants.size(), 2);
        QCOMPARE(e->variants[0].version, QString("1.0"));
        QCOMPARE(e->variants[0].stagingFolder, QString("M (1.0)"));
        QCOMPARE(e->activeVariant, 1);
        // mirrors follow the new active variant
        QCOMPARE(e->version, QString("2.0"));
        QCOMPARE(e->nexusFileId, QString("f2"));
        QCOMPARE(e->stagingFolder, QString("M (2.0)"));
        QCOMPARE(e->sourceArchive, QString("/dl/a2.zip"));
    }
    void setActiveVariantSyncsMirrors() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        ml.keepBothAddVariant("a", {"2.0", "f2", "M (2.0)", "/dl/a2.zip", true});
        QVERIFY(ml.setActiveVariant("a", 0));
        const ModEntry* e = ml.findById("a");
        QCOMPARE(e->version, QString("1.0"));
        QCOMPARE(e->stagingFolder, QString("M (1.0)"));
        QCOMPARE(e->hasFomodChoices, false);
        QVERIFY(!ml.setActiveVariant("a", 5));   // out of range
        QVERIFY(!ml.setActiveVariant("zz", 0));  // unknown id
    }
    void setActiveVariantFalseWithoutVariants() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        QVERIFY(!ml.setActiveVariant("a", 0));
    }
    void replaceWithoutVariantsOverwritesMirrors() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        QString retired;
        QVERIFY(ml.replaceActiveVersion("a", {"2.0", "f2", "M (2.0)", "/dl/a2.zip", false}, &retired));
        const ModEntry* e = ml.findById("a");
        QCOMPARE(e->version, QString("2.0"));
        QVERIFY(e->variants.isEmpty());
        QCOMPARE(retired, QString("M (1.0)"));
    }
    void replaceWithVariantsUpdatesActiveOnly() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        ml.keepBothAddVariant("a", {"2.0", "f2", "M (2.0)", "/dl/a2.zip", false});
        QString retired;
        QVERIFY(ml.replaceActiveVersion("a", {"2.1", "f3", "M (2.1)", "/dl/a3.zip", false}, &retired));
        const ModEntry* e = ml.findById("a");
        QCOMPARE(e->variants.size(), 2);
        QCOMPARE(e->variants[1].version, QString("2.1"));
        QCOMPARE(e->variants[0].version, QString("1.0")); // untouched
        QCOMPARE(e->version, QString("2.1"));
        QCOMPARE(retired, QString("M (2.0)"));
    }
    void normalizeClampsAndClears() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        ml.entries()[0].activeVariant = 3;      // corrupt: index w/o variants
        ml.normalizeVariants();
        QCOMPARE(ml.findById("a")->activeVariant, -1);
        ml.keepBothAddVariant("a", {"2.0", "f2", "M (2.0)", "/dl/a2.zip", false});
        ml.entries()[0].activeVariant = 9;      // corrupt: out of range
        ml.normalizeVariants();
        QCOMPARE(ml.findById("a")->activeVariant, 1); // clamped to last
        QCOMPARE(ml.findById("a")->version, QString("2.0")); // mirrors re-synced
    }
    void variantIndexByFileIdFindsOwner() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        ml.keepBothAddVariant("a", {"2.0", "f2", "M (2.0)", "/dl/a2.zip", false});
        QCOMPARE(ml.variantIndexByFileId("a", "f1"), 0);
        QCOMPARE(ml.variantIndexByFileId("a", "f2"), 1);
        QCOMPARE(ml.variantIndexByFileId("a", "fZ"), -1);  // no such fileId
        QCOMPARE(ml.variantIndexByFileId("a", ""), -1);    // empty fileId
        QCOMPARE(ml.variantIndexByFileId("zz", "f1"), -1); // unknown id
        // No variants -> -1 even for the mirror's fileId.
        ModList solo; solo.append(mod("b", "1.0", "f1", "M (1.0)"));
        QCOMPARE(solo.variantIndexByFileId("b", "f1"), -1);
    }
    void updateVariantActiveResyncsMirrors() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        ml.keepBothAddVariant("a", {"2.0", "f2", "M (2.0)", "/dl/a2.zip", false});
        // Variant 1 ("2.0") is active after keepBoth; update it.
        QVERIFY(ml.updateVariant("a", 1, {"2.5", "f2b", "M (2.5)", "/dl/a25.zip", true}));
        const ModEntry* e = ml.findById("a");
        QCOMPARE(e->variants[1].version, QString("2.5"));
        // Active index -> mirrors re-synced.
        QCOMPARE(e->version, QString("2.5"));
        QCOMPARE(e->nexusFileId, QString("f2b"));
        QCOMPARE(e->stagingFolder, QString("M (2.5)"));
        QCOMPARE(e->sourceArchive, QString("/dl/a25.zip"));
        QCOMPARE(e->hasFomodChoices, true);
    }
    void updateVariantInactiveLeavesMirrors() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        ml.keepBothAddVariant("a", {"2.0", "f2", "M (2.0)", "/dl/a2.zip", false});
        // Active is variant 1; update the INACTIVE variant 0.
        QVERIFY(ml.updateVariant("a", 0, {"1.1", "f1b", "M (1.1)", "/dl/a11.zip", true}));
        const ModEntry* e = ml.findById("a");
        QCOMPARE(e->variants[0].version, QString("1.1"));
        // Mirrors still reflect the active variant 1 - unchanged.
        QCOMPARE(e->version, QString("2.0"));
        QCOMPARE(e->nexusFileId, QString("f2"));
        QCOMPARE(e->stagingFolder, QString("M (2.0)"));
        // Switching to variant 0 now exposes the updated data.
        QVERIFY(ml.setActiveVariant("a", 0));
        QCOMPARE(e->version, QString("1.1"));
        QCOMPARE(e->stagingFolder, QString("M (1.1)"));
    }
    void updateVariantRejectsBadInput() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        ml.keepBothAddVariant("a", {"2.0", "f2", "M (2.0)", "/dl/a2.zip", false});
        QVERIFY(!ml.updateVariant("a", -1, {"x", "x", "x", "x", false}));
        QVERIFY(!ml.updateVariant("a", 5, {"x", "x", "x", "x", false}));
        QVERIFY(!ml.updateVariant("zz", 0, {"x", "x", "x", "x", false}));
    }
    void variantsRoundTripThroughJson() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        ml.keepBothAddVariant("a", {"2.0", "f2", "M (2.0)", "/dl/a2.zip", true});
        QTemporaryDir d; const QString p = d.path() + "/modlist.json";
        QVERIFY(ml.saveToFile(p));
        ModList back = ModList::loadFromFile(p);
        const ModEntry* e = back.findById("a");
        QVERIFY(e != nullptr);
        QCOMPARE(e->variants.size(), 2);
        QCOMPARE(e->activeVariant, 1);
        QCOMPARE(e->variants[1].hasFomodChoices, true);
        QCOMPARE(e->variants[0].nexusFileId, QString("f1"));
    }
    void oldJsonWithoutVariantsLoadsClean() {
        ModList ml; ml.append(mod("a", "1.0", "f1", "M (1.0)"));
        QTemporaryDir d; const QString p = d.path() + "/modlist.json";
        QVERIFY(ml.saveToFile(p));
        ModList back = ModList::loadFromFile(p);
        QVERIFY(back.findById("a")->variants.isEmpty());
        QCOMPARE(back.findById("a")->activeVariant, -1);
    }
};
QTEST_GUILESS_MAIN(TestModVariants)
#include "test_ModVariants.moc"
