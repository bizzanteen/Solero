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
