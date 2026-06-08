#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include "tools/ToolRunner.h"
#include "deploy/DeployRecord.h"
using namespace solero;

class TestToolRunner : public QObject {
    Q_OBJECT
private slots:
    // captureNewFiles moves brand-new runtime files into the capture target but
    // leaves deployed (record-owned) files in place even when their mtime is new.
    void captureSkipsDeployedFiles() {
        QTemporaryDir tmp;
        const QString gameDir = tmp.path() + "/game";
        const QString captureBase = gameDir + "/Data";
        const QString destBase = tmp.path() + "/overwrite";
        QVERIFY(QDir().mkpath(captureBase + "/SKSE/Plugins"));

        // runStart is a moment before we create the files, so both look "new".
        const QDateTime runStart = QDateTime::currentDateTime();
        QTest::qWait(20);

        // (a) a deployed file recorded in the deploy record (relative to gameDir).
        const QString deployedRel = "Data/SKSE/Plugins/deployed.dll";
        const QString deployedAbs = gameDir + "/" + deployedRel;
        { QFile f(deployedAbs); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("deployed"); }

        // (b) a brand-new runtime file (e.g. a shader cache) not in the record.
        const QString newAbs = captureBase + "/SKSE/Plugins/runtime.bin";
        { QFile f(newAbs); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("runtime"); }

        DeployRecord rec;
        rec.add(deployedRel, "mod-uuid-1");

        QString warning;
        int moved = ToolRunner::captureNewFiles(captureBase, destBase, gameDir,
                                                runStart, rec, &warning);

        QCOMPARE(moved, 1);
        QVERIFY(warning.isEmpty());
        // Deployed file stays put; runtime file moved into the capture target.
        QVERIFY(QFile::exists(deployedAbs));
        QVERIFY(!QFile::exists(newAbs));
        QVERIFY(QFile::exists(destBase + "/SKSE/Plugins/runtime.bin"));
        QVERIFY(!QFile::exists(destBase + "/SKSE/Plugins/deployed.dll"));
    }

    // Files older than runStart are ignored entirely. captureNewFiles floors
    // runStart to whole seconds (filesystem mtimes are often whole-second), so an
    // "old" file must predate that floored cutoff - i.e. be at least a couple of
    // seconds older - to be reliably ignored regardless of mtime granularity.
    void captureIgnoresOldFiles() {
        QTemporaryDir tmp;
        const QString gameDir = tmp.path() + "/game";
        const QString captureBase = gameDir + "/Data";
        const QString destBase = tmp.path() + "/overwrite";
        QVERIFY(QDir().mkpath(captureBase));

        const QString oldAbs = captureBase + "/old.txt";
        // Backdate the old file well before the floored runStart cutoff. setFileTime
        // requires the file open with write access, so do it before closing.
        const QDateTime runStart = QDateTime::currentDateTime();
        {
            QFile f(oldAbs);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("old");
            f.flush();
            QVERIFY(f.setFileTime(runStart.addSecs(-5),
                                  QFileDevice::FileModificationTime));
        }

        QString warning;
        int moved = ToolRunner::captureNewFiles(captureBase, destBase, gameDir,
                                                runStart, DeployRecord{}, &warning);
        QCOMPARE(moved, 0);
        QVERIFY(QFile::exists(oldAbs));
    }
};

QTEST_MAIN(TestToolRunner)
#include "test_ToolRunner.moc"
