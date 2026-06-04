#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "install/PluginScanner.h"
#include "core/ModList.h"
using namespace solero;
static void touch(const QString& p){ QDir().mkpath(QFileInfo(p).path()); QFile f(p); f.open(QIODevice::WriteOnly); f.write("x"); }
class TestPluginScanner : public QObject { Q_OBJECT
private slots:
    void scansEnabledOnlyInOrder() {
        QTemporaryDir tmp; QString s = tmp.path();
        touch(s + "/modA/Data/Aaa.esp");
        touch(s + "/modB/Data/Bbb.esm");
        touch(s + "/modC/Data/Ccc.esp");
        ModList list;
        ModEntry a; a.type=EntryType::Mod; a.id="modA"; a.name="A"; a.enabled=true;  list.append(a);
        ModEntry b; b.type=EntryType::Mod; b.id="modB"; b.name="B"; b.enabled=true;  list.append(b);
        ModEntry c; c.type=EntryType::Mod; c.id="modC"; c.name="C"; c.enabled=false; list.append(c); // disabled
        auto plugins = PluginScanner::scan(list, s);
        QCOMPARE(plugins.size(), 2);
        QCOMPARE(plugins[0], QString("Aaa.esp"));
        QCOMPARE(plugins[1], QString("Bbb.esm"));
        QVERIFY(!plugins.contains("Ccc.esp"));
    }
};
QTEST_MAIN(TestPluginScanner)
#include "test_PluginScanner.moc"
