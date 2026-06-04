#include <QtTest>
#include "tools/ToolCatalog.h"
using namespace solero;
class TestToolCatalog : public QObject { Q_OBJECT
private slots:
    void hasCommonTools() {
        QVERIFY(ToolCatalog::presets().size() >= 6);
        QVERIFY(ToolCatalog::byId("xedit") != nullptr);
        QCOMPARE(ToolCatalog::byId("xedit")->source, ToolSource::Nexus);
        QVERIFY(!ToolCatalog::byId("xedit")->author.isEmpty());
        QVERIFY(ToolCatalog::byId("radium") != nullptr);
        QCOMPARE(ToolCatalog::byId("radium")->source, ToolSource::Github);
    }
    void xeditQacReusesXeditDownload() {
        // The Quick Auto Clean entry points at the same xEdit folder via a different exe.
        auto* qac = ToolCatalog::byId("xedit-qac");
        QVERIFY(qac != nullptr);
        QVERIFY(qac->needs.contains("xedit"));
    }
};
QTEST_MAIN(TestToolCatalog)
#include "test_ToolCatalog.moc"
