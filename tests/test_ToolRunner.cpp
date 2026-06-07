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

    // Files older than runStart are ignored entirely.
    void captureIgnoresOldFiles() {
        QTemporaryDir tmp;
        const QString gameDir = tmp.path() + "/game";
        const QString captureBase = gameDir + "/Data";
        const QString destBase = tmp.path() + "/overwrite";
        QVERIFY(QDir().mkpath(captureBase));

        const QString oldAbs = captureBase + "/old.txt";
        { QFile f(oldAbs); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("old"); }
        QTest::qWait(20);
        const QDateTime runStart = QDateTime::currentDateTime();

        QString warning;
        int moved = ToolRunner::captureNewFiles(captureBase, destBase, gameDir,
                                                runStart, DeployRecord{}, &warning);
        QCOMPARE(moved, 0);
        QVERIFY(QFile::exists(oldAbs));
    }

    // gamescopeWrappedArgv prepends `gamescope <args> --` when enabled, and is a
    // strict no-op when disabled or the gamescope path is empty.
    void gamescopeWrapping() {
        const QStringList inner = {"umu-run", "/games/skse64_loader.exe", "-arg"};

        // Enabled + path present -> wrapped with tokenized args then `--`.
        QCOMPARE(ToolRunner::gamescopeWrappedArgv(true, "/usr/bin/gamescope", "-f", inner),
                 (QStringList{"/usr/bin/gamescope", "-f", "--",
                              "umu-run", "/games/skse64_loader.exe", "-arg"}));

        // Multi-token args (e.g. a forced resolution) are split correctly.
        QCOMPARE(ToolRunner::gamescopeWrappedArgv(true, "/usr/bin/gamescope",
                                                  "-f -W 1920 -H 1080", inner),
                 (QStringList{"/usr/bin/gamescope", "-f", "-W", "1920", "-H", "1080", "--",
                              "umu-run", "/games/skse64_loader.exe", "-arg"}));

        // Disabled -> unchanged.
        QCOMPARE(ToolRunner::gamescopeWrappedArgv(false, "/usr/bin/gamescope", "-f", inner), inner);
        // Enabled but gamescope absent -> unchanged.
        QCOMPARE(ToolRunner::gamescopeWrappedArgv(true, QString(), "-f", inner), inner);
    }
};

QTEST_MAIN(TestToolRunner)
#include "test_ToolRunner.moc"
