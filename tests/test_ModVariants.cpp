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
