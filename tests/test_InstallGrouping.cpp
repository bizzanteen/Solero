// Choosing the group head for a newly installed file of an already-installed
// Nexus mod, so "Cool Armour" and "Cool Armour PBR Textures" end up as one group
// rather than two unrelated rows.
#include <QtTest>
#include "install/InstallGrouping.h"
using namespace solero;

static ModEntry mod(const QString& id, const QString& name, const QString& nexusModId = {},
                    const QString& parentId = {}) {
    ModEntry m;
    m.type = EntryType::Mod;
    m.id = id;
    m.name = name;
    m.nexusModId = nexusModId;
    m.parentId = parentId;
    return m;
}

static ModEntry separator(const QString& id, const QString& name) {
    ModEntry s;
    s.type = EntryType::Separator;
    s.id = id;
    s.name = name;
    return s;
}

class TestInstallGrouping : public QObject {
    Q_OBJECT
private slots:
    void groupsUnderTheOtherFileOfTheSameMod() {
        ModList l;
        l.append(mod("armour", "Cool Armour", "111"));
        l.append(mod("pbr", "Cool Armour PBR Textures", "111"));
        QCOMPARE(installGroupParent(l, "pbr", "111"), QStringLiteral("armour"));
    }

    // The one that matters in a real list. Separators sit between the installed
    // mod and the freshly appended one, because new installs land at the bottom
    // under "NEW & UNSORTED" while the mod they belong to is sorted away above.
    void groupsAcrossSeparators() {
        ModList l;
        l.append(separator("s1", "05. ----- ARMOUR -----"));
        l.append(mod("armour", "Cool Armour", "111"));
        l.append(separator("s2", "99. ----- NEW & UNSORTED -----"));
        l.append(mod("pbr", "Cool Armour PBR Textures", "111"));
        QCOMPARE(installGroupParent(l, "pbr", "111"), QStringLiteral("armour"));
    }

    // A third file joins the existing group instead of nesting under a child.
    void groupsUnderTheHeadWhenTheMatchIsAlreadyAChild() {
        ModList l;
        l.append(mod("armour", "Cool Armour", "111"));
        l.append(mod("pbr", "Cool Armour PBR Textures", "111", "armour"));
        l.append(separator("s", "99. ----- NEW & UNSORTED -----"));
        l.append(mod("patch", "Cool Armour USSEP Patch", "111"));
        QCOMPARE(installGroupParent(l, "patch", "111"), QStringLiteral("armour"));
    }

    void doesNotGroupWhenNoOtherFileOfThatModIsInstalled() {
        ModList l;
        l.append(mod("armour", "Cool Armour", "111"));
        l.append(mod("other", "Unrelated Mod", "222"));
        QVERIFY(installGroupParent(l, "other", "222").isEmpty());
    }

    // Identity is the Nexus mod id. Two entries with no id are not the same mod
    // however similar their names are.
    void neverGroupsEntriesWithoutANexusId() {
        ModList l;
        l.append(mod("a", "Hand Installed Thing"));
        l.append(mod("b", "Hand Installed Thing 2"));
        QVERIFY(installGroupParent(l, "b", QString()).isEmpty());
    }

    void ignoresTheNewEntryItself() {
        ModList l;
        l.append(mod("only", "Cool Armour", "111"));
        QVERIFY(installGroupParent(l, "only", "111").isEmpty());
    }

    void ignoresSeparatorsAsCandidates() {
        ModList l;
        l.append(separator("s", "Cool Armour"));
        l.append(mod("pbr", "Cool Armour PBR Textures", "111"));
        QVERIFY(installGroupParent(l, "pbr", "111").isEmpty());
    }

    // The head that comes back has to be usable: groupUnder must place the child
    // directly after the parent, which is what moves it out of the unsorted bucket.
    void theChosenHeadMovesTheChildNextToIt() {
        ModList l;
        l.append(separator("s1", "05. ----- ARMOUR -----"));
        l.append(mod("armour", "Cool Armour", "111"));
        l.append(mod("boots", "Unrelated Boots", "333"));
        l.append(separator("s2", "99. ----- NEW & UNSORTED -----"));
        l.append(mod("pbr", "Cool Armour PBR Textures", "111"));

        const QString head = installGroupParent(l, "pbr", "111");
        QCOMPARE(head, QStringLiteral("armour"));
        QVERIFY(l.groupUnder("pbr", head));

        QStringList order;
        for (int i = 0; i < l.count(); ++i) order << l.at(i).id;
        QCOMPARE(order, (QStringList{"s1", "armour", "pbr", "boots", "s2"}));
        QCOMPARE(l.findById("pbr")->parentId, QStringLiteral("armour"));
    }
};

QTEST_MAIN(TestInstallGrouping)
#include "test_InstallGrouping.moc"
