// Recovering Nexus identity for a file the website downloaded itself.
#include <QtTest>
#include "nexus/DownloadMeta.h"
using namespace solero;

class TestDownloadMeta : public QObject {
    Q_OBJECT
private slots:
    void parsesTheNexusFileNameConvention() {
        auto m = nexusMetaFromFileName("eFPS - Official Patch Hub-54998-1-7a-1648632543.zip");
        QCOMPARE(m.name, QStringLiteral("eFPS - Official Patch Hub"));
        QCOMPARE(m.modId, QStringLiteral("54998"));
        QCOMPARE(m.version, QStringLiteral("1.7a"));
    }

    // The name half routinely contains dashes, so the parse anchors on the
    // trailing timestamp rather than splitting left to right.
    void keepsDashesInsideTheModName() {
        auto m = nexusMetaFromFileName(
            "Butterflies Land True - ESPFE Alternate Version-29434-1-0-1570188310.7z");
        QCOMPARE(m.name, QStringLiteral("Butterflies Land True - ESPFE Alternate Version"));
        QCOMPARE(m.modId, QStringLiteral("29434"));
        QCOMPARE(m.version, QStringLiteral("1.0"));
    }

    void leavesAnUnrecognisedNameAlone() {
        auto m = nexusMetaFromFileName("SomeManualArchive.7z");
        QCOMPARE(m.name, QStringLiteral("SomeManualArchive"));
        QVERIFY(m.modId.isEmpty());
        QVERIFY(m.version.isEmpty());
    }

    void readsIdsOffTheModPage() {
        auto m = nexusMetaFromPageUrl(QUrl(
            "https://www.nexusmods.com/skyrimspecialedition/mods/12345?tab=files&file_id=67890"));
        QCOMPARE(m.modId, QStringLiteral("12345"));
        QCOMPARE(m.fileId, QStringLiteral("67890"));
    }

    void ignoresPagesThatArentNexus() {
        auto m = nexusMetaFromPageUrl(QUrl("https://evil.example/skyrimspecialedition/mods/1?file_id=2"));
        QVERIFY(m.modId.isEmpty());
        QVERIFY(m.fileId.isEmpty());
    }

    void handlesAModPageWithNoFileId() {
        auto m = nexusMetaFromPageUrl(QUrl("https://www.nexusmods.com/skyrimspecialedition/mods/98765"));
        QCOMPARE(m.modId, QStringLiteral("98765"));
        QVERIFY(m.fileId.isEmpty());
    }

    // The page is authoritative; the filename only fills gaps.
    void pageIdsWinOverTheFileName() {
        NexusDownloadMeta page = nexusMetaFromPageUrl(QUrl(
            "https://www.nexusmods.com/skyrimspecialedition/mods/111?file_id=222"));
        NexusDownloadMeta file = nexusMetaFromFileName("Thing-999-2-5-1648632543.7z");
        auto m = mergeNexusMeta(page, file);
        QCOMPARE(m.modId, QStringLiteral("111"));    // page wins
        QCOMPARE(m.fileId, QStringLiteral("222"));
        QCOMPARE(m.version, QStringLiteral("2.5"));  // filename fills the gap
        QCOMPARE(m.name, QStringLiteral("Thing"));
    }

    void fileNameStillWorksWithNoPageContext() {
        auto m = mergeNexusMeta({}, nexusMetaFromFileName("Thing-999-2-5-1648632543.7z"));
        QCOMPARE(m.modId, QStringLiteral("999"));
        QCOMPARE(m.version, QStringLiteral("2.5"));
    }
};

QTEST_MAIN(TestDownloadMeta)
#include "test_DownloadMeta.moc"
