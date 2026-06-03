#include <QtTest>
#include "core/PluginList.h"
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
};
QTEST_MAIN(TestPluginList)
#include "test_PluginList.moc"
