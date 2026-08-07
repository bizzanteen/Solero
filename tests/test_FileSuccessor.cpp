// A Nexus page hosts several files at their own versions, so "is there an update"
// has to be asked of the installed file, not of the page.
#include <QtTest>
#include "nexus/FileSuccessor.h"
using namespace solero;

static NexusApi::NexusFile f(const QString& id, const QString& name,
                             const QString& version, const QString& category = "MAIN") {
    NexusApi::NexusFile x;
    x.fileId = id; x.name = name; x.version = version; x.category = category;
    return x;
}

class TestFileSuccessor : public QObject {
    Q_OBJECT
private slots:
    void stripsTheNexusSuffixFromAFileName() {
        QCOMPARE(normalizeNexusFileName("Cool Armour PBR Textures-12345-1-2-1700000000.7z"),
                 QStringLiteral("coolarmourpbrtextures"));
        QCOMPARE(normalizeNexusFileName("Cool Armour-12345-5-0-1700000000.7z"),
                 QStringLiteral("coolarmour"));
    }

    // "4K" and "SE" are part of the name, not a version group.
    void keepsNonNumericTrailingWords() {
        QCOMPARE(normalizeNexusFileName("SkyUI 4K SE-1-0-99.zip"), QStringLiteral("skyui4kse"));
    }

    // The reported bug: main file at 5.0, texture pack at 1.2, and the pack was
    // being offered 5.0.
    void neverOffersTheMainFilesVersionToASubmod() {
        const QList<NexusApi::NexusFile> files{
            f("100", "Cool Armour-9-5-0-17.7z", "5.0"),
            f("200", "Cool Armour PBR Textures-9-1-2-17.7z", "1.2", "OPTIONAL"),
        };
        QVERIFY(nexusSuccessorFileId(files, {}, "200").isEmpty());
    }

    // Same page, same category, and still not the same file.
    void doesNotMatchADifferentFileInTheSameCategory() {
        const QList<NexusApi::NexusFile> files{
            f("100", "Cool Armour-9-5-0-17.7z", "5.0"),
            f("200", "Cool Armour PBR Textures-9-1-2-17.7z", "1.2"),
        };
        QVERIFY(nexusSuccessorFileId(files, {}, "200").isEmpty());
    }

    void offersANewerBuildOfTheSameFile() {
        const QList<NexusApi::NexusFile> files{
            f("200", "Cool Armour PBR Textures-9-1-2-17.7z", "1.2", "OPTIONAL"),
            f("201", "Cool Armour PBR Textures-9-1-3-18.7z", "1.3", "OPTIONAL"),
        };
        QCOMPARE(nexusSuccessorFileId(files, {}, "200"), QStringLiteral("201"));
    }

    void doesNotOfferAnOlderBuildOfTheSameFile() {
        const QList<NexusApi::NexusFile> files{
            f("200", "Cool Armour PBR Textures-9-1-1-17.7z", "1.1", "OPTIONAL"),
            f("201", "Cool Armour PBR Textures-9-1-3-18.7z", "1.3", "OPTIONAL"),
        };
        QVERIFY(nexusSuccessorFileId(files, {}, "201").isEmpty());
    }

    // The author's own chain wins, even across a rename or a category move.
    void followsTheAuthorsUpdateChain() {
        const QList<NexusApi::NexusFile> files{
            f("200", "Old Name-9-1-0-17.7z", "1.0", "OLD_VERSION"),
            f("201", "Renamed Entirely-9-2-0-18.7z", "2.0"),
        };
        const QList<NexusApi::FileUpdate> updates{{"200", "201"}};
        QCOMPARE(nexusSuccessorFileId(files, updates, "200"), QStringLiteral("201"));
    }

    void followsTheChainAcrossSeveralHops() {
        const QList<NexusApi::NexusFile> files{
            f("200", "A-9-1-0-17.7z", "1.0", "OLD_VERSION"),
            f("201", "A-9-1-1-18.7z", "1.1", "OLD_VERSION"),
            f("202", "A-9-1-2-19.7z", "1.2"),
        };
        const QList<NexusApi::FileUpdate> updates{{"200", "201"}, {"201", "202"}};
        QCOMPARE(nexusSuccessorFileId(files, updates, "200"), QStringLiteral("202"));
    }

    void survivesACircularChain() {
        const QList<NexusApi::NexusFile> files{ f("200", "A-9-1-0-17.7z", "1.0") };
        const QList<NexusApi::FileUpdate> updates{{"200", "201"}, {"201", "200"}};
        QVERIFY(nexusSuccessorFileId(files, updates, "200").isEmpty()); // 201 isn't listed
    }

    // A file the page no longer lists leaves nothing to match a name against.
    void staysQuietWhenTheInstalledFileIsGone() {
        const QList<NexusApi::NexusFile> files{ f("999", "Something Else-9-2-0-17.7z", "2.0") };
        QVERIFY(nexusSuccessorFileId(files, {}, "200").isEmpty());
    }

    void staysQuietWithoutAFileId() {
        const QList<NexusApi::NexusFile> files{ f("200", "A-9-1-0-17.7z", "1.0") };
        QVERIFY(nexusSuccessorFileId(files, {}, QString()).isEmpty());
    }

    void staysQuietWhenTheInstalledFileIsAlreadyTheNewest() {
        const QList<NexusApi::NexusFile> files{
            f("200", "A-9-1-0-17.7z", "1.0", "OLD_VERSION"),
            f("201", "A-9-1-1-18.7z", "1.1"),
        };
        QVERIFY(nexusSuccessorFileId(files, {}, "201").isEmpty());
    }
};

QTEST_MAIN(TestFileSuccessor)
#include "test_FileSuccessor.moc"
