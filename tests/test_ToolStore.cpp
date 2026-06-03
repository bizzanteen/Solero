#include <QtTest>
#include <QTemporaryDir>
#include "tools/ToolStore.h"
using namespace solero;
class TestToolStore : public QObject { Q_OBJECT
private slots:
    void roundtrip() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/tools.json";
        ToolStore s(path);
        Executable e; e.id="t1"; e.name="xEdit"; e.binaryPath="/x/xEdit.exe";
        e.runtime = RuntimeType::Proton; e.protonVersion="GE-Proton9"; e.winePrefix="/pfx";
        e.isCapturingOutput = true; e.outputModId="mod-out";
        s.add(e); s.save();

        ToolStore s2(path); s2.load();
        QCOMPARE(s2.tools().size(), 1);
        QCOMPARE(s2.tools()[0].name, QString("xEdit"));
        QCOMPARE(s2.tools()[0].runtime, RuntimeType::Proton);
        QCOMPARE(s2.tools()[0].outputModId, QString("mod-out"));
    }
    void removeById() {
        QTemporaryDir tmp; ToolStore s(tmp.path()+"/t.json");
        Executable a; a.id="a"; a.name="A"; Executable b; b.id="b"; b.name="B";
        s.add(a); s.add(b); s.remove("a");
        QCOMPARE(s.tools().size(), 1);
        QCOMPARE(s.tools()[0].id, QString("b"));
    }
};
QTEST_MAIN(TestToolStore)
#include "test_ToolStore.moc"
