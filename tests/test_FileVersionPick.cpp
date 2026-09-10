// Preselecting a file when identifying a mod by name.
#include <QtTest>
#include "nexus/FileVersionPick.h"
using namespace solero;

static NexusApi::NexusFile f(const QString& id, const QString& ver, const QString& cat) {
    NexusApi::NexusFile n; n.fileId = id; n.version = ver; n.category = cat; return n;
}

class TestFileVersionPick : public QObject {
    Q_OBJECT
private slots:
    // The recorded version is the strongest hint about what's actually on disk.
    void prefersTheFileMatchingTheInstalledVersion() {
        QList<NexusApi::NexusFile> files{
            f("1", "3.0", "MAIN"), f("2", "2.1", "OLD_VERSION"), f("3", "1.4", "OLD_VERSION")};
        QCOMPARE(bestFileIndexForVersion(files, "2.1"), 1);
        QCOMPARE(bestFileIndexForVersion(files, "1.4"), 2);
    }

    void matchesVersionsCaseInsensitively() {
        QList<NexusApi::NexusFile> files{f("1", "1.0SE", "MAIN"), f("2", "2.0", "MAIN")};
        QCOMPARE(bestFileIndexForVersion(files, "1.0se"), 0);
    }

    // Nothing matches: newest MAIN, not merely the first row.
    void fallsBackToTheNewestMainFile() {
        QList<NexusApi::NexusFile> files{
            f("1", "1.0", "OPTIONAL"), f("2", "2.0", "MAIN"), f("3", "5.1", "MAIN"),
            f("4", "9.9", "OLD_VERSION")};
        QCOMPARE(bestFileIndexForVersion(files, "0.3"), 2);
        QCOMPARE(bestFileIndexForVersion(files, ""), 2);
    }

    void fallsBackToTheFirstRowWhenNothingIsMain() {
        QList<NexusApi::NexusFile> files{f("1", "1.0", "OPTIONAL"), f("2", "2.0", "MISC")};
        QCOMPARE(bestFileIndexForVersion(files, "7.0"), 0);
    }

    void handlesAnEmptyFileList() {
        QCOMPARE(bestFileIndexForVersion({}, "1.0"), 0);
    }

    // An exact match wins even when a newer MAIN exists - re-downloading is about
    // the file on disk, not the latest one.
    void anExactMatchBeatsANewerMain() {
        QList<NexusApi::NexusFile> files{f("1", "9.0", "MAIN"), f("2", "1.2", "OLD_VERSION")};
        QCOMPARE(bestFileIndexForVersion(files, "1.2"), 1);
    }
};

QTEST_MAIN(TestFileVersionPick)
#include "test_FileVersionPick.moc"
