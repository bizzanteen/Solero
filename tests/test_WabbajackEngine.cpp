#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "wabbajack/WabbajackEngine.h"
#include "core/AppConfig.h"

#include <unistd.h>  // ::link

using namespace solero;

class TestWabbajackEngine : public QObject { Q_OBJECT
private slots:

    void parseModlistsJson() {
        // Leading log lines (as the real engine emits) followed by the JSON object.
        QByteArray bytes =
            "Loading metadata...\n"
            "Some other log line\n"
            "{\n"
            "  \"metadataVersion\": \"1.0\",\n"
            "  \"count\": 2,\n"
            "  \"modlists\": [\n"
            "    {\n"
            "      \"title\": \"Alpha List\",\n"
            "      \"description\": \"desc a\",\n"
            "      \"author\": \"AuthorA\",\n"
            "      \"machineURL\": \"AuthorA/Alpha\",\n"
            "      \"game\": \"SkyrimSpecialEdition\",\n"
            "      \"gameHumanFriendly\": \"Skyrim Special Edition\",\n"
            "      \"official\": true,\n"
            "      \"nsfw\": false,\n"
            "      \"utilityList\": false,\n"
            "      \"version\": \"1.2.3\",\n"
            "      \"tags\": [\"Performance\", \"Graphics\"],\n"
            "      \"links\": {\n"
            "        \"image\": \"https://example.com/a.webp\",\n"
            "        \"readme\": \"https://example.com/a-readme\",\n"
            "        \"websiteURL\": \"https://example.com/a-site\"\n"
            "      },\n"
            "      \"sizes\": {\n"
            "        \"downloadSize\": 1000,\n"
            "        \"downloadSizeFormatted\": \"1.0 GB\",\n"
            "        \"installSize\": 2000,\n"
            "        \"installSizeFormatted\": \"2.0 GB\",\n"
            "        \"totalSizeFormatted\": \"3.0 GB\",\n"
            "        \"numberOfArchives\": 42\n"
            "      }\n"
            "    },\n"
            "    {\n"
            "      \"title\": \"Beta List\",\n"
            "      \"author\": \"AuthorB\",\n"
            "      \"machineURL\": \"AuthorB/Beta\",\n"
            "      \"game\": \"Fallout4\",\n"
            "      \"gameHumanFriendly\": \"Fallout 4\",\n"
            "      \"official\": false,\n"
            "      \"nsfw\": true,\n"
            "      \"utilityList\": true,\n"
            "      \"version\": \"0.9\",\n"
            "      \"tags\": [],\n"
            "      \"links\": { \"image\": \"https://example.com/b.webp\" },\n"
            "      \"sizes\": { \"downloadSizeFormatted\": \"5.5 GB\", \"installSizeFormatted\": \"10 GB\" }\n"
            "    }\n"
            "  ]\n"
            "}\n";

        QString err;
        auto lists = WabbajackEngine::parseModlistsJson(bytes, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(lists.size(), 2);

        const auto& a = lists[0];
        QCOMPARE(a.title, QString("Alpha List"));
        QCOMPARE(a.author, QString("AuthorA"));
        QCOMPARE(a.machineUrl, QString("AuthorA/Alpha"));
        QCOMPARE(a.gameHuman, QString("Skyrim Special Edition"));
        QCOMPARE(a.imageUrl, QString("https://example.com/a.webp"));
        QCOMPARE(a.readmeUrl, QString("https://example.com/a-readme"));
        QCOMPARE(a.websiteUrl, QString("https://example.com/a-site"));
        QCOMPARE(a.downloadSizeStr, QString("1.0 GB"));
        QCOMPARE(a.installSizeStr, QString("2.0 GB"));
        QCOMPARE(a.official, true);
        QCOMPARE(a.nsfw, false);
        QCOMPARE(a.utility, false);
        QCOMPARE(a.tags, QStringList({"Performance", "Graphics"}));

        const auto& b = lists[1];
        QCOMPARE(b.title, QString("Beta List"));
        QCOMPARE(b.nsfw, true);
        QCOMPARE(b.utility, true);
        QCOMPARE(b.downloadSizeStr, QString("5.5 GB"));
        QVERIFY(b.tags.isEmpty());
    }

    void parseModlistsJson_noJson() {
        QString err;
        auto lists = WabbajackEngine::parseModlistsJson("just logs, no json\n", &err);
        QVERIFY(lists.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    void parseSizeToBytes_units() {
        const double tol = 1.0;  // absolute byte tolerance for float rounding
        QVERIFY(qAbs(WabbajackEngine::parseSizeToBytes("233.3MB")
                     - 233.3 * 1024.0 * 1024.0) < 1024.0);
        QVERIFY(qAbs(WabbajackEngine::parseSizeToBytes("0.1GB")
                     - 0.1 * 1024.0 * 1024.0 * 1024.0) < 1024.0);
        QVERIFY(qAbs(WabbajackEngine::parseSizeToBytes("1024KB")
                     - 1024.0 * 1024.0) < tol);
        QVERIFY(qAbs(WabbajackEngine::parseSizeToBytes("5GB")
                     - 5.0 * 1024.0 * 1024.0 * 1024.0) < tol);
        // Whitespace and case tolerance.
        QVERIFY(qAbs(WabbajackEngine::parseSizeToBytes(" 2.0 gb ")
                     - 2.0 * 1024.0 * 1024.0 * 1024.0) < tol);
        // Garbage -> -1.
        QCOMPARE(WabbajackEngine::parseSizeToBytes("abc"), -1.0);
        QCOMPARE(WabbajackEngine::parseSizeToBytes("12"), -1.0);  // no unit
    }

    void parseProgress_installingFiles() {
        QString op; double pct = -1; double rem = 999;
        bool ok = WabbajackEngine::parseProgressLine(
            "Installing files 819/1497 (233.3MB/276.7MB) - foo.bsa", op, pct, rem);
        QVERIFY(ok);
        // pct must be the BYTE ratio (~84.3), not the count ratio (~54.7).
        const double byteRatio = 100.0 * (233.3 * 1024.0 * 1024.0)
                                       / (276.7 * 1024.0 * 1024.0);
        QVERIFY2(qAbs(pct - byteRatio) < 1e-3, qPrintable(QString::number(pct)));
        QVERIFY2(qAbs(pct - (100.0 * 819 / 1497)) > 10.0,
                 qPrintable(QString::number(pct)));
        QCOMPARE(rem, -1.0);
        QVERIFY2(op.contains("233.3MB"), qPrintable(op));
        QVERIFY2(op.contains("276.7MB"), qPrintable(op));
    }

    void parseProgress_downloadingArchives() {
        QString op; double pct = 999; double rem = -1;
        bool ok = WabbajackEngine::parseProgressLine(
            "Downloading Mod Archives (3/10) - 20.3MB/s - 0.1GB remaining",
            op, pct, rem);
        QVERIFY(ok);
        // Download lines leave pct=-1 (the caller computes it from the run peak).
        QCOMPARE(pct, -1.0);
        QVERIFY2(qAbs(rem - 0.1 * 1024.0 * 1024.0 * 1024.0) < 1024.0,
                 qPrintable(QString::number(rem)));
        QVERIFY2(op.startsWith("Downloading archives 3/10"), qPrintable(op));
        QVERIFY2(op.contains("0.1GB remaining"), qPrintable(op));
    }

    void parseProgress_downloadingZeroTotal() {
        QString op; double pct = 999; double rem = -1;
        bool ok = WabbajackEngine::parseProgressLine(
            "Downloading Mod Archives (0/1) - 20.3MB/s - 0.1GB remaining",
            op, pct, rem);
        QVERIFY(ok);
        QCOMPARE(pct, -1.0);  // caller computes the download pct
        QVERIFY(!op.isEmpty());
        QVERIFY2(qAbs(rem - 0.1 * 1024.0 * 1024.0 * 1024.0) < 1024.0,
                 qPrintable(QString::number(rem)));
    }

    void parseProgress_phaseBanner() {
        QString op; double pct = 999; double rem = 999;
        bool ok = WabbajackEngine::parseProgressLine("=== Installing ===",
                                                     op, pct, rem);
        QVERIFY(ok);
        QCOMPARE(op, QString("Installing"));
        QVERIFY(pct < 0); // banner has no percentage
        QCOMPARE(rem, -1.0);
    }

    void parseProgress_noMatch() {
        QString op; double pct = -1; double rem = -1;
        QVERIFY(!WabbajackEngine::parseProgressLine(
            "Loading metadata, please wait...", op, pct, rem));
        QVERIFY(!WabbajackEngine::parseProgressLine("", op, pct, rem));
    }

    void parseFailedArchives_anvilShapes() {
        // Representative slice of a real Anvil (WakingDreams/ANVIL) failure log:
        // several Tools_* Creation-Kit (GameFileSource) lines, one MEGA line, and
        // one Wabbajack-CDN line. Interleaved with ordinary progress/log noise.
        const QString log = QStringLiteral(
            "=== Downloading ===\n"
            "Downloading Mod Archives (0/50) - 20.3MB/s - 12.0GB remaining\n"
            "Unable to download Tools_CreationKit (GameFileSourceDownloader+State|SkyrimSpecialEdition|1.6.1170.0|Tools\\Pad\\CreationKit.exe)\n"
            "Unable to download Tools_Papyrus (GameFileSourceDownloader+State|SkyrimSpecialEdition|1.6.1170.0|Papyrus Compiler\\PapyrusCompiler.exe)\n"
            "Unable to download Tools_LexDict (GameFileSourceDownloader+State|SkyrimSpecialEdition|1.6.1170.0|Tools\\lex_en.dic)\n"
            "Unable to download High Poly Head 1.4 SE.7z (MegaDownloader+State|https://mega.nz/file/abc123#KEYKEYKEY)\n"
            "Unable to download xLODGen.130.7z (WabbajackCDNDownloader+State|https://wabbajack.b-cdn.net/xLODGen.130.7z)\n"
            "Some other unrelated log line\n");

        const auto failed = WabbajackEngine::parseFailedArchives(log);

        QCOMPARE(failed.size(), 5);

        int gfs = 0, mega = 0, cdn = 0;
        for (const auto& fa : failed) {
            switch (fa.source) {
            case FailedSource::GameFileSource: ++gfs; break;
            case FailedSource::Mega:           ++mega; break;
            case FailedSource::WabbajackCDN:   ++cdn; break;
            default: break;
            }
        }
        QVERIFY2(gfs >= 3, qPrintable(QString::number(gfs)));
        QCOMPARE(mega, 1);
        QCOMPARE(cdn, 1);

        // GameFileSource field extraction (first CK row).
        const FailedArchive* ck = nullptr;
        for (const auto& fa : failed)
            if (fa.source == FailedSource::GameFileSource) { ck = &fa; break; }
        QVERIFY(ck);
        QCOMPARE(ck->name, QString("Tools_CreationKit"));
        QCOMPARE(ck->game, QString("SkyrimSpecialEdition"));
        QCOMPARE(ck->version, QString("1.6.1170.0"));
        QCOMPARE(ck->path, QString("Tools\\Pad\\CreationKit.exe")); // backslashes preserved

        // MEGA url extraction.
        const FailedArchive* mg = nullptr;
        for (const auto& fa : failed)
            if (fa.source == FailedSource::Mega) { mg = &fa; break; }
        QVERIFY(mg);
        QCOMPARE(mg->name, QString("High Poly Head 1.4 SE.7z"));
        QCOMPARE(mg->url, QString("https://mega.nz/file/abc123#KEYKEYKEY"));

        // Wabbajack-CDN url extraction.
        const FailedArchive* wc = nullptr;
        for (const auto& fa : failed)
            if (fa.source == FailedSource::WabbajackCDN) { wc = &fa; break; }
        QVERIFY(wc);
        QCOMPARE(wc->name, QString("xLODGen.130.7z"));
        QCOMPARE(wc->url, QString("https://wabbajack.b-cdn.net/xLODGen.130.7z"));
    }

    void parseFailedArchives_httpAndNexusAndOther() {
        const QString log = QStringLiteral(
            "Unable to download SomeMod.zip (HttpDownloader|https://example.com/SomeMod.zip)\n"
            "Unable to download NexusMod-1234 (NexusDownloader+State|12345|67890)\n"
            "Unable to download Mystery.dat (WeirdDownloader+State|foo|bar)\n"
            "Unable to download Skimpy Outfit from Nexus (adult content gated)\n");

        const auto failed = WabbajackEngine::parseFailedArchives(log);

        int http = 0, nexus = 0, other = 0;
        const FailedArchive* httpFa = nullptr;
        for (const auto& fa : failed) {
            switch (fa.source) {
            case FailedSource::Http:  ++http; httpFa = &fa; break;
            case FailedSource::Nexus: ++nexus; break;
            case FailedSource::Other: ++other; break;
            default: break;
            }
        }
        QCOMPARE(http, 1);
        QVERIFY2(nexus >= 1, qPrintable(QString::number(nexus)));
        QCOMPARE(other, 1);
        QVERIFY(httpFa);
        QCOMPARE(httpFa->url, QString("https://example.com/SomeMod.zip"));
    }

    void parseFailedArchives_failedToDownloadGameFile() {
        // The StandardInstaller's per-archive failure shape (em-dash U+2014):
        //   Failed to download '<name>' (Source: <Source>) - <reason>
        const QString log = QString::fromUtf8(
            "00:00:08.374 [ERROR] (Wabbajack.Installer.StandardInstaller) "
            "Failed to download 'Data_ccbgssse037-curios.esl' (Source: GameFileSource) "
            "- Game file not found: "
            "/var/home/eamon/.local/share/Steam/steamapps/common/Skyrim Special Edition/Data/ccbgssse037-curios.esl "
            "(Game: SkyrimSpecialEdition)\n");

        const auto failed = WabbajackEngine::parseFailedArchives(log);

        QCOMPARE(failed.size(), 1);
        const FailedArchive& fa = failed.first();
        QCOMPARE(fa.source, FailedSource::GameFileSource);
        QCOMPARE(fa.name, QString("Data_ccbgssse037-curios.esl"));
        QCOMPARE(fa.game, QString("SkyrimSpecialEdition"));
        QCOMPARE(fa.path, QString("/var/home/eamon/.local/share/Steam/"
                                  "steamapps/common/Skyrim Special Edition/Data/"
                                  "ccbgssse037-curios.esl"));
    }

    void parseFailedArchives_vfsPrimingWrongHash() {
        // The StandardInstaller's VFS-priming per-archive failure shape. This is
        // the line emitted when a Creation Club file is present but the wrong build.
        const QString log = QStringLiteral(
            "00:00:16.223 [ERROR] (Wabbajack.Installer.StandardInstaller) "
            "VFS priming failed: Archive 'Data_ccbgssse037-curios.bsa' "
            "(hash: FQbA20bA5Dw=) not found in HashedArchives. "
            "File exists with correct size but wrong hash (corrupted or modified).\n"
            "00:00:16.223 [ERROR] (Wabbajack.Installer.StandardInstaller) "
            "VFS priming failed due to missing archives. Installation cannot continue.\n");

        const auto failed = WabbajackEngine::parseFailedArchives(log);

        QCOMPARE(failed.size(), 1);
        const FailedArchive& fa = failed.first();
        QCOMPARE(fa.source, FailedSource::GameFileSource);
        QCOMPARE(fa.name, QString("Data_ccbgssse037-curios.bsa"));
        QVERIFY(fa.wrongHash);
        QVERIFY(WabbajackEngine::isCreationClub(fa.name));

        // A non-Creation-Club game file should not be flagged as CC.
        QVERIFY(!WabbajackEngine::isCreationClub(QStringLiteral("Data_skyrim.esm")));
    }

    void parseFailedArchives_empty() {
        QVERIFY(WabbajackEngine::parseFailedArchives(
            "no failures here, all good\n").isEmpty());
    }

    void removeRedundantLowercaseCCLinks_dedupsOnlyHardlinkDups() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QDir dir(tmp.path());

        auto writeFile = [&](const QString& name, const QByteArray& content) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(content);
            f.close();
        };

        // (a) proper-case + all-lowercase hard link of the same inode -> dup, remove.
        writeFile("ccBGSSSE001-Fish.esm", "fish-master-data");
        QVERIFY(::link(QFile::encodeName(dir.filePath("ccBGSSSE001-Fish.esm")).constData(),
                       QFile::encodeName(dir.filePath("ccbgssse001-fish.esm")).constData())
                == 0);

        // (b) lowercase-only, no proper-case sibling (Rare Curios) -> keep.
        writeFile("ccbgssse037-curios.esl", "curios-data");

        // (c) two DIFFERENT-content case variants, not hard-linked -> keep both.
        writeFile("ccX.esm", "content-X-uppercase");
        writeFile("ccx.esm", "content-x-lowercase");

        const QStringList removed =
            WabbajackEngine::removeRedundantLowercaseCCLinks(tmp.path());

        // (a): the lowercase dup is gone, the proper-case canonical kept.
        QVERIFY2(removed.contains("ccbgssse001-fish.esm"), qPrintable(removed.join(',')));
        QVERIFY(!dir.exists("ccbgssse001-fish.esm"));
        QVERIFY(dir.exists("ccBGSSSE001-Fish.esm"));

        // (b): lowercase-only with no sibling is untouched.
        QVERIFY(!removed.contains("ccbgssse037-curios.esl"));
        QVERIFY(dir.exists("ccbgssse037-curios.esl"));

        // (c): non-hardlinked differently-cased pair - both kept, neither removed.
        QVERIFY(!removed.contains("ccx.esm"));
        QVERIFY(dir.exists("ccX.esm"));
        QVERIFY(dir.exists("ccx.esm"));

        // Only (a) was removed.
        QCOMPARE(removed.size(), 1);
    }

    void removeRedundantLowercaseCCLinks_missingDir() {
        // Non-existent / empty dir is a no-op, not a crash.
        QCOMPARE(WabbajackEngine::removeRedundantLowercaseCCLinks(QString()).size(), 0);
        QCOMPARE(WabbajackEngine::removeRedundantLowercaseCCLinks(
                     "/no/such/path/zzz").size(), 0);
    }

    void findEngine_configuredPath() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QString fake = tmp.path() + "/jackify-engine";
        {
            QFile f(fake);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("#!/bin/sh\nexit 0\n");
        }
        QFile::setPermissions(fake, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner);

        QString prev = AppConfig::instance().jackifyEnginePath();
        AppConfig::instance().setJackifyEnginePath(fake);
        QCOMPARE(WabbajackEngine::findEngine(), QFileInfo(fake).absoluteFilePath());
        AppConfig::instance().setJackifyEnginePath(prev);
    }
};

QTEST_MAIN(TestWabbajackEngine)
#include "test_WabbajackEngine.moc"
