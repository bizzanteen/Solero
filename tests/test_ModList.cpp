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
    void roundtripJson_preservesData() {
        ModList list;
        ModEntry sep; sep.type = EntryType::Separator; sep.id = "s1"; sep.name = "Weapons"; sep.color = "#ff0000"; sep.icon = "⚔";
        ModEntry mod; mod.type = EntryType::Mod; mod.id = "m1"; mod.name = "Heavy Armory"; mod.enabled = true; mod.version = "1.2";
        list.append(sep);
        list.append(mod);

        QJsonDocument doc = list.toJson();
        ModList restored = ModList::fromJson(doc);
        QCOMPARE(restored.count(), 2);
        QCOMPARE(restored.at(0).name, "Weapons");
        QCOMPARE(restored.at(1).name, "Heavy Armory");
        QCOMPARE(restored.at(1).version, "1.2");
    }
};
QTEST_MAIN(TestModList)
#include "test_ModList.moc"
