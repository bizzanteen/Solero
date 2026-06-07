#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "core/PluginList.h"
#include "core/Profile.h"
using namespace solero;

class TestPluginList : public QObject {
    Q_OBJECT
private slots:
    void appendPlugin_increasesCount() {
        PluginList list;
        PluginEntry p; p.filename = "SkyUI.esp"; p.enabled = true;
        list.append(p);
        QCOMPARE(list.count(), 1);
    }
    void move_changesOrder() {
        PluginList list;
        for (auto name : {"A.esp", "B.esp", "C.esp"}) {
            PluginEntry p; p.filename = name; p.enabled = true;
            list.append(p);
        }
        list.move(0, 2);
        QCOMPARE(list.at(2).filename, QString("A.esp"));
    }
    void roundtripPluginsTxt() {
        PluginList list;
        PluginEntry a; a.filename = "Skyrim.esm"; a.enabled = true;  a.isMaster = true;
        PluginEntry b; b.filename = "USSEP.esp";  b.enabled = true;
        PluginEntry c; c.filename = "Disabled.esp"; c.enabled = false;
        list.append(a); list.append(b); list.append(c);

        QString txt = list.toPluginsTxt();
        PluginList restored = PluginList::fromPluginsTxt(txt);
        QCOMPARE(restored.count(), 3);
        QCOMPARE(restored.at(0).filename, QString("Skyrim.esm"));
        QCOMPARE(restored.at(1).enabled, true);
        QCOMPARE(restored.at(2).enabled, false);
    }

    // (i) A pinned plugin returns to its pinned index after a non-manual reorder.
    void pin_restoresAfterReorder() {
        PluginList list;
        for (auto name : {"A.esp", "B.esp", "C.esp", "D.esp"}) {
            PluginEntry p; p.filename = name; p.enabled = true;
            list.append(p);
        }
        list.setPinned("C.esp", true);             // records index 2
        QCOMPARE(list.pinnedIndex("C.esp"), 2);
        // Simulate a sort that shuffles the order (C drops to index 1).
        list.restoreSnapshot({{"D.esp", true}, {"C.esp", true},
                              {"B.esp", true}, {"A.esp", true}});
        QCOMPARE(list.at(1).filename, QString("C.esp")); // moved by the reorder
        list.applyPins();
        QCOMPARE(list.at(2).filename, QString("C.esp")); // pin restored it to 2
    }

    // (ii) A pin that would violate the official/band order is clamped to a legal
    // slot: it never rises above the officials or breaks master<light<esp.
    void pin_clampedToBand() {
        PluginList list;
        auto add = [&](const char* fn, bool official, bool master) {
            PluginEntry p; p.filename = fn; p.enabled = true;
            p.isOfficial = official; p.isMaster = master;
            list.append(p);
        };
        add("Skyrim.esm", true,  true);   // official master, index 0
        add("Big.esm",    false, true);   // master band,     index 1
        add("ModA.esp",   false, false);  // esp band,        index 2
        add("ModB.esp",   false, false);  // esp band,        index 3
        // Pin ModB to index 0 - illegal (above officials, inside the master band).
        QHash<QString, int> pins; pins.insert("modb.esp", 0);
        list.setPinnedIndices(pins);
        list.applyPins();
        // Officials + masters keep the top; ModB is clamped into the esp band.
        QCOMPARE(list.at(0).filename, QString("Skyrim.esm"));
        QCOMPARE(list.at(1).filename, QString("Big.esm"));
        QCOMPARE(list.at(2).filename, QString("ModB.esp"));
        QVERIFY(!list.at(2).isOfficial);
    }

    // (iii) The lock flag (and pins) round-trip through Profile persistence, and
    // an absent state file is backward-compatible (unlocked, no pins).
    void lockAndPins_roundTripThroughProfile() {
        QTemporaryDir tmp;
        {
            Profile p("Lock", tmp.path());
            PluginEntry a; a.filename = "Skyrim.esm"; a.isMaster = true;
            PluginEntry b; b.filename = "Mod.esp";
            p.pluginList().append(a);
            p.pluginList().append(b);
            p.pluginList().setLoadOrderLocked(true);
            p.pluginList().setPinned("Mod.esp", true); // index 1
            QVERIFY(p.save());
            QVERIFY(QFile::exists(p.loadOrderStatePath()));
        }
        Profile p2("Lock", tmp.path());
        QVERIFY(p2.load());
        QVERIFY(p2.pluginList().loadOrderLocked());
        QCOMPARE(p2.pluginList().pinnedIndex("Mod.esp"), 1);

        // Unlock + unpin, save, reload -> defaults restored.
        p2.pluginList().setLoadOrderLocked(false);
        p2.pluginList().setPinned("Mod.esp", false);
        QVERIFY(p2.save());
        Profile p3("Lock", tmp.path());
        QVERIFY(p3.load());
        QVERIFY(!p3.pluginList().loadOrderLocked());
        QCOMPARE(p3.pluginList().pinnedIndex("Mod.esp"), -1);
    }

    // Missing loadorder-state.json is backward-compatible: unlocked, no pins.
    void loadOrderState_missingIsDefault() {
        QTemporaryDir tmp;
        Profile p("NoState", tmp.path());
        QVERIFY(p.load());
        QVERIFY(!p.pluginList().loadOrderLocked());
        QVERIFY(p.pluginList().pinnedIndices().isEmpty());
    }
};
QTEST_MAIN(TestPluginList)
#include "test_PluginList.moc"
