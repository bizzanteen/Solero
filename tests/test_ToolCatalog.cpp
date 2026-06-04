#include <QtTest>
#include "tools/ToolCatalog.h"
using namespace solero;
class TestToolCatalog : public QObject { Q_OBJECT
private slots:
    void hasCommonTools() {
        QCOMPARE(ToolCatalog::presets().size(), 7);
        QVERIFY(ToolCatalog::byId("xedit") != nullptr);
        QCOMPARE(ToolCatalog::byId("xedit")->source, ToolSource::Nexus);
        QVERIFY(!ToolCatalog::byId("xedit")->author.isEmpty());
        QVERIFY(ToolCatalog::byId("radium") != nullptr);
        QCOMPARE(ToolCatalog::byId("radium")->source, ToolSource::Github);
    }
    void xeditRunModes() {
        // Quick Auto Clean and Quick Show Conflicts are now extraActions on the single xedit preset.
        QVERIFY(ToolCatalog::byId("xedit-qac") == nullptr);
        auto* xe = ToolCatalog::byId("xedit");
        QVERIFY(xe != nullptr);
        QCOMPARE(xe->extraActions.size(), 2);
        QCOMPARE(xe->extraActions[0].label, QString("Quick Auto Clean"));
        QCOMPARE(xe->extraActions[0].exeRelPath, QString("SSEEditQuickAutoClean.exe"));
        QCOMPARE(xe->extraActions[1].label, QString("Quick Show Conflicts"));
    }
    void dyndolodMergedTexGen() {
        auto* dd = ToolCatalog::byId("dyndolod");
        QVERIFY(dd != nullptr);
        QCOMPARE(dd->extraActions.size(), 1);
        QCOMPARE(dd->extraActions[0].label, QString("Run TexGen"));
        QCOMPARE(dd->extraActions[0].exeRelPath, QString("TexGenx64.exe"));
        QVERIFY(ToolCatalog::byId("texgen") == nullptr);
        QVERIFY(ToolCatalog::byId("dyndolod-res") == nullptr);
    }
    void nativeToolsNoProton() {
        QVERIFY(ToolCatalog::byId("synthesis") != nullptr);
        QCOMPARE(ToolCatalog::byId("synthesis")->proton, false);
        QVERIFY(ToolCatalog::byId("radium") != nullptr);
        QCOMPARE(ToolCatalog::byId("radium")->proton, false);
        QCOMPARE(ToolCatalog::byId("xedit")->proton, true);
    }
    void pgpatcherRenamed() {
        auto* pg = ToolCatalog::byId("pgpatcher");
        QVERIFY(pg != nullptr);
        QCOMPARE(pg->githubRepo, QString("PGPatcher"));
    }
    void allHaveIconResource() {
        for (const auto& t : ToolCatalog::presets())
            QVERIFY(t.iconResource.startsWith(":/icons/tools/"));
    }
};
QTEST_MAIN(TestToolCatalog)
#include "test_ToolCatalog.moc"
