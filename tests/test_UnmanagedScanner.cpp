#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "deploy/UnmanagedScanner.h"
#include "deploy/DeployRecord.h"

using namespace solero;

namespace {
void writeFile(const QString& path, const QByteArray& data = "x") {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
    f.close();
}
} // namespace

class TestUnmanagedScanner : public QObject {
    Q_OBJECT
private slots:
    // The core scenario: a game dir with a vanilla file (baseline), a Solero-deployed
    // file (record), Solero metadata, and two files a run created afterwards. Only the
    // two new files must be reported.
    void detectsOnlyNewUnmanagedFiles() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString game = tmp.path();

        // Pre-run state: vanilla + a deployed mod file + Solero's own metadata.
        writeFile(game + "/Data/Skyrim.esm");                       // vanilla
        writeFile(game + "/Data/MyMod.esp");                        // deployed
        writeFile(game + "/.solero-deployed.json", "{}");           // record file
        writeFile(game + "/.solero-backup/Data/Skyrim.esm");        // backup tree

        const QSet<QString> baseline = snapshotGameFiles(game);
        // Baseline excludes Solero metadata but includes the two real files.
        QVERIFY(baseline.contains("Data/Skyrim.esm"));
        QVERIFY(baseline.contains("Data/MyMod.esp"));
        QVERIFY(!baseline.contains(".solero-deployed.json"));
        QVERIFY(!baseline.contains(".solero-backup/Data/Skyrim.esm"));
        QCOMPARE(baseline.size(), 2);

        DeployRecord rec;
        rec.add("Data/MyMod.esp", "mod-a");

        // A run writes two new files.
        writeFile(game + "/Data/SKSE/Plugins/generated.txt");
        writeFile(game + "/Data/ENBCache/cache.bin");

        const QStringList unmanaged = findUnmanagedFiles(game, rec, baseline);
        QCOMPARE(unmanaged, (QStringList{"Data/ENBCache/cache.bin",
                                         "Data/SKSE/Plugins/generated.txt"}));
    }

    // A managed file present on disk with different letter-casing than the record key
    // (Wine/Proton case-insensitivity) must not be misreported as unmanaged.
    void managedMatchIsCaseInsensitive() {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        const QString game = tmp.path();
        writeFile(game + "/data/casemod.esp"); // on-disk lowercased
        DeployRecord rec;
        rec.add("Data/CaseMod.esp", "mod-a");  // recorded mixed-case
        const QStringList unmanaged = findUnmanagedFiles(game, rec, {});
        QVERIFY(unmanaged.isEmpty()); // treated as managed, not new
    }

    // With an empty baseline, every non-managed, non-metadata file is reported (a
    // one-off "what's loose here?" scan), including vanilla files.
    void emptyBaselineReportsAllUnmanaged() {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        const QString game = tmp.path();
        writeFile(game + "/Data/Skyrim.esm");   // not in any record
        writeFile(game + "/Data/MyMod.esp");    // managed
        writeFile(game + "/.solero-deployed.json", "{}");
        DeployRecord rec;
        rec.add("Data/MyMod.esp", "mod-a");
        const QStringList unmanaged = findUnmanagedFiles(game, rec, {});
        QCOMPARE(unmanaged, (QStringList{"Data/Skyrim.esm"})); // vanilla surfaces; .solero excluded
    }

    // A missing/empty game dir yields empty results, never a crash.
    void missingDirIsEmpty() {
        DeployRecord rec;
        QVERIFY(snapshotGameFiles(QString()).isEmpty());
        QVERIFY(snapshotGameFiles("/no/such/solero/dir").isEmpty());
        QVERIFY(findUnmanagedFiles("/no/such/solero/dir", rec, {}).isEmpty());
    }
};

QTEST_MAIN(TestUnmanagedScanner)
#include "test_UnmanagedScanner.moc"
