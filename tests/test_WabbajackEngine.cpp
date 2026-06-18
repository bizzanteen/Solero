#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "wabbajack/WabbajackEngine.h"
#include "core/AppConfig.h"

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

    void parseProgress_installingFiles() {
        QString op; double pct = -1;
        bool ok = WabbajackEngine::parseProgressLine(
            "Installing files 819/1497 (233.3MB/276.7MB) - foo.bsa", op, pct);
        QVERIFY(ok);
        QVERIFY(qAbs(pct - (100.0 * 819 / 1497)) < 1e-6);
        QCOMPARE(op, QString("Installing files 819/1497"));
    }

    void parseProgress_downloadingArchives() {
        QString op; double pct = -1;
        bool ok = WabbajackEngine::parseProgressLine(
            "Downloading Mod Archives (3/10) - 20.3MB/s - 0.1GB remaining", op, pct);
        QVERIFY(ok);
        QVERIFY(qAbs(pct - 30.0) < 1e-6);
        QVERIFY2(op.startsWith("Downloading archives 3/10"), qPrintable(op));
        QVERIFY2(op.contains("0.1GB remaining"), qPrintable(op));
    }

    void parseProgress_downloadingZeroTotal() {
        // n/total with total>0 but n=0 -> 0%; op still valid.
        QString op; double pct = -1;
        bool ok = WabbajackEngine::parseProgressLine(
            "Downloading Mod Archives (0/1) - 20.3MB/s - 0.1GB remaining", op, pct);
        QVERIFY(ok);
        QVERIFY(qAbs(pct - 0.0) < 1e-6);
    }

    void parseProgress_phaseBanner() {
        QString op; double pct = 999;
        bool ok = WabbajackEngine::parseProgressLine("=== Installing ===", op, pct);
        QVERIFY(ok);
        QCOMPARE(op, QString("Installing"));
        QVERIFY(pct < 0); // banner has no percentage
    }

    void parseProgress_noMatch() {
        QString op; double pct = -1;
        QVERIFY(!WabbajackEngine::parseProgressLine(
            "Loading metadata, please wait...", op, pct));
        QVERIFY(!WabbajackEngine::parseProgressLine("", op, pct));
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

    void parseFailedArchives_empty() {
        QVERIFY(WabbajackEngine::parseFailedArchives(
            "no failures here, all good\n").isEmpty());
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
