#include <QtTest>
#include <QTemporaryDir>
#include "tools/DotNetSdk.h"
using namespace solero;

// Deciding whether the prefix's .NET SDK is new enough for the patchers Synthesis
// wants to build. A prefix can hold several SDKs side by side, one folder each.
class TestDotNetSdk : public QObject {
    Q_OBJECT
private slots:
    void readsTheMajorVersion() {
        QCOMPARE(sdkMajor("10.0.302"), 10);
        QCOMPARE(sdkMajor("8.0.423"), 8);
    }

    // Preview/RC folder names carry a suffix.
    void readsTheMajorFromAPreviewVersion() {
        QCOMPARE(sdkMajor("10.0.100-preview.1.25080.5"), 10);
    }

    void returnsZeroForNonVersions() {
        QCOMPARE(sdkMajor(""), 0);
        QCOMPARE(sdkMajor("NuGetFallbackFolder"), 0);
        QCOMPARE(sdkMajor("banana"), 0);
    }

    // The one that matters: comparing these as strings puts "8.0.423" above
    // "10.0.302", which would report a prefix as up to date when it is two majors
    // behind - exactly the "SDK Out of Date" failure this is meant to prevent.
    void picksTheHighestMajorNumericallyNotAlphabetically() {
        QCOMPARE(highestSdkMajor({"8.0.423", "10.0.302"}), 10);
        QCOMPARE(highestSdkMajor({"10.0.302", "8.0.423"}), 10);
        QCOMPARE(highestSdkMajor({"9.0.316", "8.0.423"}), 9);
    }

    void handlesNoSdksAtAll() {
        QCOMPARE(highestSdkMajor({}), 0);
        QCOMPARE(highestSdkMajor({"NuGetFallbackFolder"}), 0);
    }

    // Synthesis surfaces this as "NET SDK Out of Date"; the log carries NETSDK1045.
    void spotsTheTooOldSdkError() {
        QVERIFY(showsSdkTooOld(
            "Microsoft.NET.TargetFrameworkInference.targets(166,5): error NETSDK1045: "
            "The current .NET SDK does not support targeting .NET 10.0."));
        QVERIFY(!showsSdkTooOld("Build succeeded."));
        QVERIFY(!showsSdkTooOld("error NU3028: not trusted"));
    }

    void findsTheTooOldSdkErrorInAToolLogFromThisRun() {
        QTemporaryDir tmp;
        const QString installDir = tmp.path() + "/synthesis";
        QDir().mkpath(installDir + "/logs");
        QFile f(installDir + "/logs/Current.log");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("error NETSDK1045: does not support targeting .NET 10.0\n");
        f.close();
        const QDateTime mtime = QFileInfo(f).lastModified();
        QVERIFY(sdkTooOldInToolLogs(installDir, mtime.addSecs(-5)));
        QVERIFY(!sdkTooOldInToolLogs(installDir, mtime.addSecs(60))); // stale log
        QVERIFY(!sdkTooOldInToolLogs(tmp.path() + "/nope", mtime.addSecs(-5)));
    }
};

QTEST_MAIN(TestDotNetSdk)
#include "test_DotNetSdk.moc"
