#include <QtTest>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include "core/AppConfig.h"
#include "tools/PrefixCertFix.h"
using namespace solero;

// The VeriSign Universal Root CA that NuGet needs to validate nuget.org's
// repository signatures. Fedora (and Debian, and Arch) dropped it from the system
// bundle, and Wine seeds a Proton prefix's trust store from that bundle - so the
// prefix has no way to validate the signature or its timestamp, and every restore
// fails with NU3028 + NU3037.
//
// Fixtures below are taken from a prefix where the fix was applied by hand and
// verified to make `dotnet restore` succeed, so these tests compare against a
// registry entry that is known to actually work - not against a re-derivation of
// the same code.
class TestPrefixCertFix : public QObject {
    Q_OBJECT

    // SHA1 of the known-good Root\Certificates key name, and of the working Blob.
    static QString knownThumbprint() {
        return "3679CA35668772304D30A5FB873B0FA77BB70D54";
    }
    static QString knownBlobSha256() {
        return "8E4B5816389C25EF79F1B7D46FC328A94925A97DF2E8995B69326E7880AE344F";
    }
    static void writeFile(const QString& p, const QByteArray& c) {
        QDir().mkpath(QFileInfo(p).path());
        QFile f(p); QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(p)); f.write(c);
    }

private slots:
    // The bundled copy must be the real certificate. If this ever fails, the blob
    // being installed into people's prefixes is not the cert we think it is.
    void bundledCertIsTheVerisignUniversalRoot() {
        const QByteArray der = PrefixCertFix::universalRootDer();
        QCOMPARE(der.size(), 1213);
        QCOMPARE(PrefixCertFix::thumbprint(der), knownThumbprint());
    }

    // Windows stores a cert as a sequence of property records:
    // <u32 propId><u32 encoding><u32 cbData><data>. Property 3 is the SHA1 hash,
    // property 32 is the DER. Byte-compare the header against the working entry.
    void blobUsesTheWindowsPropertyRecordLayout() {
        const QByteArray blob = PrefixCertFix::certBlob(PrefixCertFix::universalRootDer());
        const QByteArray head = QByteArray::fromHex(
            "030000000100000014000000"                  // propId 3, encoding 1, 20 bytes
            "3679CA35668772304D30A5FB873B0FA77BB70D54"  // the SHA1 hash itself
            "200000000100000" "0bd040000");             // propId 32, encoding 1, 1213 bytes
        QCOMPARE(blob.left(44), head);
    }

    // ...and the whole blob must be byte-identical to the one that works.
    void blobMatchesTheKnownGoodRegistryEntry() {
        const QByteArray blob = PrefixCertFix::certBlob(PrefixCertFix::universalRootDer());
        QCOMPARE(blob.size(), 1257);
        QCOMPARE(QString::fromLatin1(
                     QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toHex().toUpper()),
                 knownBlobSha256());
    }

    // The .reg must target HKLM's Root store under the thumbprint key name, which
    // is how Windows and Wine name certs in a system store.
    void registryTextTargetsTheRootStoreUnderTheThumbprint() {
        const QString reg = PrefixCertFix::registryFileText(PrefixCertFix::universalRootDer());
        QVERIFY(reg.startsWith("REGEDIT4"));
        QVERIFY(reg.contains("[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\SystemCertificates"
                             "\\Root\\Certificates\\" + knownThumbprint() + "]"));
        QVERIFY(reg.contains("\"Blob\"=hex:"));
        // Every byte of the blob has to survive into the file.
        QString hex;
        for (const QChar c : reg.section("\"Blob\"=hex:", 1))
            if (c.isLetterOrNumber()) hex.append(c);
        QCOMPARE(QByteArray::fromHex(hex.toLatin1()).size(), 1257);
    }

    // Recognising an already-fixed prefix is what makes the whole thing idempotent.
    void detectsTheCertInAPrefixThatHasIt() {
        QTemporaryDir tmp;
        const QString prefix = tmp.path() + "/compatdata";
        writeFile(prefix + "/pfx/system.reg",
            "WINE REGISTRY Version 2\n\n"
            "[Software\\\\Microsoft\\\\SystemCertificates\\\\Root\\\\Certificates\\\\"
            "3679CA35668772304D30A5FB873B0FA77BB70D54] 1785824510\n"
            "\"Blob\"=hex:03,00,00,00\n");
        QVERIFY(PrefixCertFix::isInstalled(prefix, PrefixCertFix::universalRootDer()));
    }

    void reportsMissingInAPrefixWithoutIt() {
        QTemporaryDir tmp;
        const QString prefix = tmp.path() + "/compatdata";
        writeFile(prefix + "/pfx/system.reg",
            "WINE REGISTRY Version 2\n\n"
            "[Software\\\\Microsoft\\\\SystemCertificates\\\\Root\\\\Certificates\\\\"
            "0F36385B811A25C39B314E83CAE9346670CC74B4] 1760398946\n"
            "\"Blob\"=hex:03,00,00,00\n");
        QVERIFY(!PrefixCertFix::isInstalled(prefix, PrefixCertFix::universalRootDer()));
        // A prefix that doesn't exist at all is "not installed", not a crash.
        QVERIFY(!PrefixCertFix::isInstalled(tmp.path() + "/nope",
                                            PrefixCertFix::universalRootDer()));
    }

    // Re-applying must be a no-op rather than a second import: install() reports
    // alreadyPresent and never shells out to wine.
    void installIsANoOpWhenTheCertIsAlreadyThere() {
        QTemporaryDir tmp;
        const QString prefix = tmp.path() + "/compatdata";
        writeFile(prefix + "/pfx/system.reg",
            "WINE REGISTRY Version 2\n\n"
            "[Software\\\\Microsoft\\\\SystemCertificates\\\\Root\\\\Certificates\\\\"
            "3679CA35668772304D30A5FB873B0FA77BB70D54] 1785824510\n"
            "\"Blob\"=hex:03,00,00,00\n");
        const auto r = PrefixCertFix::install(prefix);
        QVERIFY(r.ok);
        QVERIFY(r.alreadyPresent);
    }

    // End-to-end against a real throwaway Wine prefix. Opt-in: it needs Proton
    // installed and takes about a minute to build the prefix, so it stays out of
    // the normal run. This is the only coverage of the actual reg.exe import, so
    // run it when touching install():
    //   SOLERO_TEST_WINE_PREFIX=1 ./tests/test_PrefixCertFix
    void installsIntoARealWinePrefix() {
        if (qEnvironmentVariableIsEmpty("SOLERO_TEST_WINE_PREFIX"))
            QSKIP("set SOLERO_TEST_WINE_PREFIX=1 to exercise a real Wine prefix");

        const QString protonDir = AppConfig::instance().detectProtonDir();
        QVERIFY2(!protonDir.isEmpty(), "no Proton install found");
        const QString wine = protonDir + "/files/bin/wine";
        QVERIFY2(QFile::exists(wine), qPrintable(wine));

        QTemporaryDir tmp;
        const QString prefix = tmp.path() + "/compatdata";
        QDir().mkpath(prefix + "/pfx");

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("WINEPREFIX", prefix + "/pfx");
        env.insert("WINEDEBUG", "-all");
        QProcess boot;
        boot.setProcessEnvironment(env);
        boot.setProcessChannelMode(QProcess::MergedChannels);
        boot.start(wine, {"wineboot.exe", "-i"});
        QVERIFY(boot.waitForStarted(30000));
        QVERIFY2(boot.waitForFinished(300000), "prefix creation timed out");

        // A freshly created prefix seeds its trust store from the host bundle, which
        // on this distro no longer carries the VeriSign Universal Root - that
        // absence IS the bug.
        QVERIFY2(!PrefixCertFix::isInstalled(prefix, PrefixCertFix::universalRootDer()),
                 "host bundle still has the root; this distro isn't affected");

        QString log;
        const auto first = PrefixCertFix::install(prefix, &log);
        QVERIFY2(first.ok, qPrintable(first.error + "\n" + log));
        QVERIFY(!first.alreadyPresent);
        QVERIFY(PrefixCertFix::isInstalled(prefix, PrefixCertFix::universalRootDer()));

        // Second run changes nothing.
        const auto second = PrefixCertFix::install(prefix);
        QVERIFY(second.ok);
        QVERIFY(second.alreadyPresent);
    }

    // The detection predicate that decides whether to offer the fix at all.
    void spotsTheNuGetTrustFailureInToolOutput() {
        QVERIFY(PrefixCertFix::outputShowsNuGetTrustFailure(
            "error NU3028: Package 'Mutagen.Bethesda' ... The repository countersignature's "
            "timestamping certificate is not trusted by the trust provider."));
        QVERIFY(PrefixCertFix::outputShowsNuGetTrustFailure(
            "error NU3037: ... The repository countersignature validity period has expired."));
    }

    // Synthesis is a GUI app that logs through Serilog to logs/Current.log, so the
    // NuGet errors never reach stdout - checking only the captured output would
    // mean the detection never fires for the one tool that needs it.
    void findsTheFailureInAToolLogWrittenDuringTheRun() {
        QTemporaryDir tmp;
        const QString installDir = tmp.path() + "/synthesis";
        writeFile(installDir + "/logs/Current.log",
            "2026-08-04 13:17:35.534 +07:00 [ERR] VersionQuery.csproj : error NU3028: "
            "Package 'Mutagen.Bethesda' ... is not trusted by the trust provider.\n");
        const QDateTime before = QFileInfo(installDir + "/logs/Current.log")
                                     .lastModified().addSecs(-5);
        QVERIFY(PrefixCertFix::toolLogShowsNuGetTrustFailure(installDir, before));
    }

    // A log left over from an earlier run must not re-trigger the offer: the run
    // that just happened may have failed for a completely different reason.
    void ignoresAToolLogFromABeforeTheRun() {
        QTemporaryDir tmp;
        const QString installDir = tmp.path() + "/synthesis";
        writeFile(installDir + "/logs/Current.log", "error NU3028: stale failure\n");
        const QDateTime after = QFileInfo(installDir + "/logs/Current.log")
                                    .lastModified().addSecs(60);
        QVERIFY(!PrefixCertFix::toolLogShowsNuGetTrustFailure(installDir, after));
    }

    void handlesToolsWithNoLogsAtAll() {
        QTemporaryDir tmp;
        QVERIFY(!PrefixCertFix::toolLogShowsNuGetTrustFailure(
            tmp.path() + "/no-such-tool", QDateTime::currentDateTime().addSecs(-60)));
    }

    void ignoresOutputWithoutThatFailure() {
        QVERIFY(!PrefixCertFix::outputShowsNuGetTrustFailure(""));
        QVERIFY(!PrefixCertFix::outputShowsNuGetTrustFailure(
            "Build succeeded.\n0 Warning(s)\n0 Error(s)\n"));
        // A different NuGet diagnostic is a different problem - don't offer a cert
        // fix for a missing package or an unrelated signature warning.
        QVERIFY(!PrefixCertFix::outputShowsNuGetTrustFailure(
            "error NU1101: Unable to find package Foo. No packages exist with this id."));
        QVERIFY(!PrefixCertFix::outputShowsNuGetTrustFailure(
            "warning NU3027: The signature should be timestamped."));
        // Must not fire on a code that merely starts with the same digits.
        QVERIFY(!PrefixCertFix::outputShowsNuGetTrustFailure("error NU30281: something else"));
    }
};

QTEST_MAIN(TestPrefixCertFix)
#include "test_PrefixCertFix.moc"
