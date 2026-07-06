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

    // ---- v2: source fingerprint + version + deploy mode ----

    void fingerprint_roundtrips() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/.solero-deployed.json";
        DeployRecord rec;
        rec.add("Data/foo.esp", "mod-a", 12345, 1699999999000LL);
        rec.setDeployMode(2); // e.g. Copy
        rec.saveToFile(path);

        DeployRecord loaded = DeployRecord::loadFromFile(path);
        QCOMPARE(loaded.ownerOf("Data/foo.esp"), QString("mod-a"));
        QVERIFY(loaded.contains("Data/foo.esp"));
        auto fp = loaded.fingerprintOf("Data/foo.esp");
        QVERIFY(fp.valid());
        QCOMPARE(fp.size, qint64(12345));
        QCOMPARE(fp.mtimeMs, qint64(1699999999000LL));
        QCOMPARE(loaded.version(), 2);
        QCOMPARE(loaded.deployMode(), 2);
    }

    void freshRecord_isVersion2() {
        DeployRecord rec;
        QCOMPARE(rec.version(), 2);
    }

    void twoArgAdd_hasNoFingerprint() {
        DeployRecord rec;
        rec.add("Plugins.txt", "__solero_generated__");
        QVERIFY(rec.contains("Plugins.txt"));
        QVERIFY(!rec.fingerprintOf("Plugins.txt").valid());
    }

    void missingPath_fingerprintInvalid() {
        DeployRecord rec;
        QVERIFY(!rec.fingerprintOf("nope").valid());
        QVERIFY(!rec.contains("nope"));
    }

    // A legacy v1 file (flat relPath -> "modId" strings, no "version"/"files"
    // wrapper) must still load: owners preserved, but reported as version 1 with
    // no fingerprints and unknown mode, so deploy() forces one full redeploy.
    void legacyV1_loadsAsVersion1_noFingerprints() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/.solero-deployed.json";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"Data/foo.esp":"mod-a","Data/bar.nif":"mod-b"})");
        f.close();

        DeployRecord loaded = DeployRecord::loadFromFile(path);
        QCOMPARE(loaded.version(), 1);
        QCOMPARE(loaded.deployMode(), -1);
        QCOMPARE(loaded.ownerOf("Data/foo.esp"), QString("mod-a"));
        QCOMPARE(loaded.ownerOf("Data/bar.nif"), QString("mod-b"));
        QVERIFY(!loaded.fingerprintOf("Data/foo.esp").valid());
    }
};
QTEST_MAIN(TestDeployRecord)
#include "test_DeployRecord.moc"
