#include <QtTest>
#include "core/ModList.h"
using namespace solero;

class TestModList : public QObject {
    Q_OBJECT
private slots:
    void appendMod_increasesCount() {
        ModList list;
        ModEntry m; m.type = EntryType::Mod; m.id = "a"; m.name = "Noble Skyrim"; m.enabled = true;
        list.append(m);
        QCOMPARE(list.count(), 1);
    }
    void appendSeparator_countIncludesSeparators() {
        ModList list;
        ModEntry sep; sep.type = EntryType::Separator; sep.id = "s1"; sep.name = "Textures";
        list.append(sep);
        QCOMPARE(list.count(), 1);
    }
    void moveMod_changesPosition() {
        ModList list;
        for (int i = 0; i < 3; ++i) {
            ModEntry m; m.type = EntryType::Mod; m.id = QString::number(i); m.name = QString("Mod%1").arg(i);
            list.append(m);
        }
        list.move(0, 2);
        QCOMPARE(list.at(2).id, "0");
    }
    void disableParentNexusGroup_disablesChildren() {
        ModList list;
        ModEntry parent; parent.type = EntryType::Mod; parent.id = "p"; parent.nexusModId = "12345"; parent.parentId = ""; parent.enabled = true;
        ModEntry child;  child.type  = EntryType::Mod; child.id  = "c"; child.nexusModId = "12345"; child.parentId = "p"; child.enabled = true;
        list.append(parent);
        list.append(child);
        list.setEnabled("p", false);
        QCOMPARE(list.findById("c")->enabled, false);
    }
    void moveSection_carriesSeparatorAndItsMods() {
        // Layout: [sep1, m0, m1, sep2]. Dragging the first separator below the
        // second must carry its two mods along, giving [sep2, sep1, m0, m1].
        ModList list;
        ModEntry sep1; sep1.type = EntryType::Separator; sep1.id = "sep1"; sep1.name = "A";
        ModEntry m0;   m0.type   = EntryType::Mod;       m0.id   = "m0";   m0.name   = "Mod0";
        ModEntry m1;   m1.type   = EntryType::Mod;       m1.id   = "m1";   m1.name   = "Mod1";
        ModEntry sep2; sep2.type = EntryType::Separator; sep2.id = "sep2"; sep2.name = "B";
        list.append(sep1); list.append(m0); list.append(m1); list.append(sep2);

        // Block = sep1 + its 2 mods (raw 0, length 3). Drop after sep2: with the
        // block removed the list is [sep2], so the insertion index is 1.
        list.moveSection(0, 3, 1);

        QCOMPARE(list.count(), 4);
        QCOMPARE(list.at(0).id, "sep2");
        QCOMPARE(list.at(1).id, "sep1");
        QCOMPARE(list.at(2).id, "m0");
        QCOMPARE(list.at(3).id, "m1");
    }
    // --- Multi-file grouping (Stage M2) ---------------------------------------
    static QString rawOrder(const ModList& list) {
        QStringList ids;
        for (int i = 0; i < list.count(); ++i) ids << list.at(i).id;
        return ids.join(",");
    }
    static ModEntry makeMod(const QString& id, const QString& nexusId = QString()) {
        ModEntry m; m.type = EntryType::Mod; m.id = id; m.name = "Mod " + id;
        m.enabled = true; m.nexusModId = nexusId; return m;
    }

    void autoGroup_secondSameNexus_becomesContiguousChild() {
        // Simulate install auto-group: two mods share nexusModId "100"; the second
        // nests under the first. A third with the same id becomes a 2nd child after
        // the first child. A mod with a unique id stays top-level.
        ModList list;
        list.append(makeMod("a", "100"));
        list.append(makeMod("u", "999")); // unique, stays top-level
        list.append(makeMod("b", "100"));
        // b shares 100 with a -> group under a.
        list.groupUnder("b", "a");
        QCOMPARE(rawOrder(list), QString("a,b,u")); // child contiguous after parent
        QCOMPARE(list.findById("b")->parentId, QString("a"));
        QCOMPARE(list.childRunCount(0), 1);

        // Third file for the same Nexus mod -> second child, after the first child.
        list.append(makeMod("c", "100"));
        list.groupUnder("c", "a");
        QCOMPARE(rawOrder(list), QString("a,b,c,u"));
        QCOMPARE(list.findById("c")->parentId, QString("a"));
        QCOMPARE(list.childRunCount(0), 2);

        // The unique mod is untouched / not a child.
        QVERIFY(list.findById("u")->parentId.isEmpty());
    }

    void ungroup_clearsParent_andPlacesAfterBlock() {
        // [a, b(child of a), c(child of a), u]. Ungroup b -> it loses parentId and
        // moves to just after the remaining group block (after c).
        ModList list;
        list.append(makeMod("a", "100"));
        list.append(makeMod("b", "100"));
        list.append(makeMod("c", "100"));
        list.append(makeMod("u", "999"));
        list.groupUnder("b", "a");
        list.groupUnder("c", "a");
        QCOMPARE(rawOrder(list), QString("a,b,c,u"));

        list.ungroup("b");
        QVERIFY(list.findById("b")->parentId.isEmpty());
        QCOMPARE(rawOrder(list), QString("a,c,b,u")); // b sits right after the block
        QCOMPARE(list.childRunCount(0), 1); // only c remains a child

        // Ungroup the last remaining child -> parent is no longer a group head.
        list.ungroup("c");
        QVERIFY(list.findById("c")->parentId.isEmpty());
        QCOMPARE(list.childRunCount(0), 0);
        QCOMPARE(rawOrder(list), QString("a,c,b,u"));
    }

    void groupSelected_firstIsParent_restContiguousChildren() {
        // Group 3 mods scattered in the list: a (first) parent, then d and b as
        // contiguous children right after a, in the given order.
        ModList list;
        list.append(makeMod("a"));
        list.append(makeMod("x")); // bystander
        list.append(makeMod("b"));
        list.append(makeMod("y")); // bystander
        list.append(makeMod("d"));
        // First selected = a (parent); group d then b under it.
        list.groupUnder("d", "a");
        list.groupUnder("b", "a");
        // a + its children contiguous; bystanders x,y preserved (order may shift as
        // children are pulled forward, but x and y stay top-level).
        QCOMPARE(list.findById("d")->parentId, QString("a"));
        QCOMPARE(list.findById("b")->parentId, QString("a"));
        QCOMPARE(list.childRunCount(0), 2);
        // Children stored contiguously immediately after a, in selection order.
        QCOMPARE(list.at(0).id, QString("a"));
        QCOMPARE(list.at(1).id, QString("d"));
        QCOMPARE(list.at(2).id, QString("b"));
        QVERIFY(list.findById("x")->parentId.isEmpty());
        QVERIFY(list.findById("y")->parentId.isEmpty());
    }

    void findByNexusId_returnsMatch_respectsSkip() {
        ModList list;
        list.append(makeMod("a", "100"));
        list.append(makeMod("b", "100"));
        ModEntry sep; sep.type = EntryType::Separator; sep.id = "100"; // id collides w/ nexus id on purpose
        list.append(sep);
        // First Mod with nexusModId "100" is "a".
        QVERIFY(list.findByNexusId("100") != nullptr);
        QCOMPARE(list.findByNexusId("100")->id, QString("a"));
        // Skipping "a" returns the next match "b".
        QCOMPARE(list.findByNexusId("100", "a")->id, QString("b"));
        // Separators are ignored even if their id equals the nexus id.
        QVERIFY(list.findByNexusId("999") == nullptr);
        // Empty nexus id never matches.
        QVERIFY(list.findByNexusId(QString()) == nullptr);
    }

    void findByName_caseInsensitive_respectsSkip() {
        ModList list;
        ModEntry m1; m1.type = EntryType::Mod; m1.id = "1"; m1.name = "Noble Skyrim";
        ModEntry m2; m2.type = EntryType::Mod; m2.id = "2"; m2.name = "Noble Skyrim";
        list.append(m1);
        list.append(m2);
        // Case-insensitive name match returns the first entry.
        QVERIFY(list.findByName("noble skyrim") != nullptr);
        QCOMPARE(list.findByName("NOBLE skyrim")->id, QString("1"));
        // Skipping the first returns the duplicate-named second.
        QCOMPARE(list.findByName("Noble Skyrim", "1")->id, QString("2"));
        // No match -> nullptr.
        QVERIFY(list.findByName("Unknown Mod") == nullptr);
    }

    void roundtripJson_preservesData() {
        ModList list;
        ModEntry sep; sep.type = EntryType::Separator; sep.id = "s1"; sep.name = "Weapons"; sep.color = "#ff0000"; sep.icon = "⚔";
        ModEntry mod; mod.type = EntryType::Mod; mod.id = "m1"; mod.name = "Heavy Armory"; mod.enabled = true; mod.version = "1.2";
        mod.note = "Patched ESL flag\nremember to redeploy";
        list.append(sep);
        list.append(mod);

        QJsonDocument doc = list.toJson();
        ModList restored = ModList::fromJson(doc);
        QCOMPARE(restored.count(), 2);
        QCOMPARE(restored.at(0).name, "Weapons");
        QCOMPARE(restored.at(1).name, "Heavy Armory");
        QCOMPARE(restored.at(1).version, "1.2");
        QCOMPARE(restored.at(1).note, QString("Patched ESL flag\nremember to redeploy"));
    }

    void note_absentInOlderJson_defaultsEmpty() {
        // A modlist.json written before the note field existed has no "note" key.
        const QByteArray legacy =
            "[{\"type\":\"mod\",\"id\":\"m1\",\"name\":\"Old Mod\",\"enabled\":true}]";
        ModList restored = ModList::fromJson(QJsonDocument::fromJson(legacy));
        QCOMPARE(restored.count(), 1);
        QVERIFY(restored.at(0).note.isEmpty());
    }
};
QTEST_MAIN(TestModList)
#include "test_ModList.moc"
