// One-time pass for mods installed before auto-grouping worked across separators.
#include <QtTest>
#include "install/GroupExistingMods.h"
using namespace solero;

static ModEntry mod(const QString& id, const QString& name, const QString& nexusModId = {},
                    const QString& parentId = {}) {
    ModEntry m;
    m.type = EntryType::Mod;
    m.id = id; m.name = name; m.nexusModId = nexusModId; m.parentId = parentId;
    return m;
}

static ModEntry separator(const QString& id, const QString& name) {
    ModEntry s; s.type = EntryType::Separator; s.id = id; s.name = name;
    return s;
}

static QStringList idsOf(const ModList& l) {
    QStringList out;
    for (int i = 0; i < l.count(); ++i) out << l.at(i).id;
    return out;
}

class TestGroupExistingMods : public QObject {
    Q_OBJECT
private slots:
    // Wyrmstooth and its three patches, sitting apart across separators.
    void nestsTheExtraFilesUnderTheFirstEntry() {
        ModList l;
        l.append(separator("s1", "10. ----- QUESTS -----"));
        l.append(mod("wyrm", "Wyrmstooth", "45565"));
        l.append(mod("other", "Unrelated", "999"));
        l.append(separator("s2", "99. ----- NEW & UNSORTED -----"));
        l.append(mod("db", "Wyrmstooth Dragonborn Patch", "45565"));
        l.append(mod("hf", "Wyrmstooth Hearthfire Patch", "45565"));

        QCOMPARE(groupExistingMultiFileMods(l), 2);
        QCOMPARE(idsOf(l), (QStringList{"s1", "wyrm", "db", "hf", "other", "s2"}));
        QCOMPARE(l.findById("db")->parentId, QStringLiteral("wyrm"));
        QCOMPARE(l.findById("hf")->parentId, QStringLiteral("wyrm"));
        QVERIFY(l.findById("wyrm")->parentId.isEmpty());
    }

    // A group the user already arranged is theirs; don't re-shuffle it.
    void leavesAnAlreadyGroupedModAlone() {
        ModList l;
        l.append(mod("head", "Cool Armour", "111"));
        l.append(mod("child", "Cool Armour PBR", "111", "head"));
        l.append(separator("s", "99. ----- NEW -----"));
        l.append(mod("third", "Cool Armour Patch", "111"));

        QCOMPARE(groupExistingMultiFileMods(l), 0);
        QCOMPARE(idsOf(l), (QStringList{"head", "child", "s", "third"}));
    }

    void ignoresSingleFileModsAndEntriesWithoutANexusId() {
        ModList l;
        l.append(mod("a", "Alone", "1"));
        l.append(mod("b", "Hand installed"));
        l.append(mod("c", "Also hand installed"));
        QCOMPARE(groupExistingMultiFileMods(l), 0);
        QCOMPARE(idsOf(l), (QStringList{"a", "b", "c"}));
    }

    void isANoOpOnASecondRun() {
        ModList l;
        l.append(mod("head", "Cool Armour", "111"));
        l.append(separator("s", "99. ----- NEW -----"));
        l.append(mod("pbr", "Cool Armour PBR", "111"));

        QCOMPARE(groupExistingMultiFileMods(l), 1);
        QCOMPARE(groupExistingMultiFileMods(l), 0);
        QCOMPARE(idsOf(l), (QStringList{"head", "pbr", "s"}));
    }

    void handlesAnEmptyList() {
        ModList l;
        QCOMPARE(groupExistingMultiFileMods(l), 0);
    }
};

QTEST_MAIN(TestGroupExistingMods)
#include "test_GroupExistingMods.moc"
