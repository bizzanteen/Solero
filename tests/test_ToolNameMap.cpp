#include <QtTest>
#include "tools/ToolNameMap.h"
#include "core/Types.h"
using namespace solero;

static ModEntry mod(const QString& name, const QString& id, bool out = false) {
    ModEntry e; e.type = EntryType::Mod; e.name = name; e.id = id; e.isOutputMod = out;
    return e;
}

class TestToolNameMap : public QObject { Q_OBJECT
private slots:
    void mapsByName() {
        QCOMPARE(presetIdForToolName("DynDOLOD", ""),        QString("dyndolod"));
        QCOMPARE(presetIdForToolName("SSEEdit", ""),         QString("xedit"));
        QCOMPARE(presetIdForToolName("xEdit", ""),           QString("xedit"));
        QCOMPARE(presetIdForToolName("Nemesis Unlimited Behavior Engine", ""),
                 QString("nemesis"));
        QCOMPARE(presetIdForToolName("Pandora Behaviour Engine", ""), QString("pandora"));
        QCOMPARE(presetIdForToolName("Synthesis", ""),       QString("synthesis"));
        QCOMPARE(presetIdForToolName("ESLifier", ""),        QString("eslifier"));
        QCOMPARE(presetIdForToolName("PGPatcher", ""),       QString("pgpatcher"));
        QCOMPARE(presetIdForToolName("Radium Textures", ""), QString("radium"));
    }
    void mapsByBinaryBasename() {
        // Title may be generic ("Play"); fall back to the binary basename.
        QCOMPARE(presetIdForToolName("Play", "/i/tools/xEdit/SSEEdit.exe"), QString("xedit"));
        QCOMPARE(presetIdForToolName("", "/i/mods/Nemesis/Nemesis Unlimited Behavior Engine.exe"),
                 QString("nemesis"));
        QCOMPARE(presetIdForToolName("", "/i/tools/DynDOLOD/DynDOLODx64.exe"), QString("dyndolod"));
    }
    void texGenMapsToDyndolod() {
        QCOMPARE(presetIdForToolName("TexGen", "/i/tools/DynDOLOD/TexGenx64.exe"),
                 QString("dyndolod"));
    }
    void caseInsensitive() {
        QCOMPARE(presetIdForToolName("dyndolod", ""), QString("dyndolod"));
        QCOMPARE(presetIdForToolName("SSEEDIT", ""),  QString("xedit"));
    }
    void unmappedReturnsEmpty() {
        QVERIFY(presetIdForToolName("Bethini", "/i/tools/Bethini Pie/Bethini.exe").isEmpty());
        QVERIFY(presetIdForToolName("Explorer++", "/i/explorer++/Explorer++.exe").isEmpty());
        QVERIFY(presetIdForToolName("", "").isEmpty());
    }

    void findsExactOutputMod() {
        // A discovered tool with a "DynDOLOD Output" mod in the profile resolves
        // to that mod (no creation).
        QList<ModEntry> mods{
            mod("Some Texture Mod", "m1"),
            mod("DynDOLOD Output", "out-dd"),
            mod("DynDOLOD Resources", "res-dd"),
        };
        QCOMPARE(findOutputModId(mods, "DynDOLOD"), QString("out-dd"));
    }
    void findsFuzzyOutputMod() {
        QList<ModEntry> mods{
            mod("Nemesis Output Files", "out-nem", /*out=*/true),
        };
        QCOMPARE(findOutputModId(mods, "Nemesis"), QString("out-nem"));
    }
    void excludesResourcesAndMisses() {
        QList<ModEntry> mods{
            mod("DynDOLOD Resources SE", "res"),   // Resource -> excluded
            mod("Unrelated Mod", "u"),
        };
        QVERIFY(findOutputModId(mods, "DynDOLOD").isEmpty());
        QVERIFY(findOutputModId({}, "Anything").isEmpty());
    }
};
QTEST_MAIN(TestToolNameMap)
#include "test_ToolNameMap.moc"
