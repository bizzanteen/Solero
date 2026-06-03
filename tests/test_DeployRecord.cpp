#include <QtTest>
#include <QTemporaryDir>
#include "deploy/DeployRecord.h"
using namespace solero;

class TestDeployRecord : public QObject {
    Q_OBJECT
private slots:
    void addAndQuery() {
        DeployRecord rec;
        rec.add("Data/meshes/foo.nif", "mod-uuid-1");
        rec.add("Data/textures/bar.dds", "mod-uuid-2");
        QCOMPARE(rec.ownerOf("Data/meshes/foo.nif"), QString("mod-uuid-1"));
        QCOMPARE(rec.ownerOf("Data/textures/bar.dds"), QString("mod-uuid-2"));
        QCOMPARE(rec.ownerOf("nonexistent"), QString());
    }
    void allPaths_returnsAllKeys() {
        DeployRecord rec;
        rec.add("a.txt", "m1");
        rec.add("b.txt", "m2");
        auto paths = rec.allPaths();
        QVERIFY(paths.contains("a.txt"));
        QVERIFY(paths.contains("b.txt"));
        QCOMPARE(paths.size(), 2);
    }
    void roundtripJson() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/.solero-deployed.json";
        DeployRecord rec;
        rec.add("Data/foo.esp", "mod-a");
        rec.add("Data/bar.nif", "mod-b");
        rec.saveToFile(path);

        DeployRecord loaded = DeployRecord::loadFromFile(path);
        QCOMPARE(loaded.ownerOf("Data/foo.esp"), QString("mod-a"));
        QCOMPARE(loaded.ownerOf("Data/bar.nif"), QString("mod-b"));
    }
    void clear_emptiesRecord() {
        DeployRecord rec;
        rec.add("a", "m"); rec.clear();
        QCOMPARE(rec.allPaths().size(), 0);
    }
};
QTEST_MAIN(TestDeployRecord)
#include "test_DeployRecord.moc"
