// Entry names come from whatever wrote the archive: Windows separators, drive
// letters, traversal segments.
#include <QtTest>
#include "install/ArchivePath.h"
using namespace solero;

class TestArchivePath : public QObject {
    Q_OBJECT
private slots:
    void leavesAnOrdinaryRelativePathAlone() {
        QCOMPARE(sanitizeArchiveEntryPath("Data/SKSE/Plugins/foo.dll"),
                 QStringLiteral("Data/SKSE/Plugins/foo.dll"));
        QCOMPARE(sanitizeArchiveEntryPath("readme.txt"), QStringLiteral("readme.txt"));
    }

    // One entry name, three path components.
    void turnsWindowsSeparatorsIntoDirectories() {
        QCOMPARE(sanitizeArchiveEntryPath("SKSE\\Plugins\\main_menu_randomizer.dll"),
                 QStringLiteral("SKSE/Plugins/main_menu_randomizer.dll"));
    }

    void handlesAMixtureOfSeparators() {
        QCOMPARE(sanitizeArchiveEntryPath("Data/SKSE\\Plugins/foo.dll"),
                 QStringLiteral("Data/SKSE/Plugins/foo.dll"));
    }

    void stripsADriveLetter() {
        QCOMPARE(sanitizeArchiveEntryPath("C:\\mods\\thing\\foo.dll"),
                 QStringLiteral("mods/thing/foo.dll"));
        QCOMPARE(sanitizeArchiveEntryPath("d:/mods/foo.dll"),
                 QStringLiteral("mods/foo.dll"));
    }

    void stripsLeadingSlashesSoNothingIsAbsolute() {
        QCOMPARE(sanitizeArchiveEntryPath("/etc/passwd"), QStringLiteral("etc/passwd"));
        QCOMPARE(sanitizeArchiveEntryPath("\\\\server\\share\\foo.dll"),
                 QStringLiteral("server/share/foo.dll"));
    }

    void collapsesEmptyAndDotComponents() {
        QCOMPARE(sanitizeArchiveEntryPath("Data//SKSE/./foo.dll"),
                 QStringLiteral("Data/SKSE/foo.dll"));
    }

    void rejectsTraversal() {
        QVERIFY(sanitizeArchiveEntryPath("../../evil.dll").isEmpty());
        QVERIFY(sanitizeArchiveEntryPath("..\\..\\evil.dll").isEmpty());
        QVERIFY(sanitizeArchiveEntryPath("Data/../../evil.dll").isEmpty());
        QVERIFY(sanitizeArchiveEntryPath("/../evil.dll").isEmpty());
    }

    // Normalises to nothing, so there's nowhere to write it.
    void rejectsAnEntryThatNormalisesToNothing() {
        QVERIFY(sanitizeArchiveEntryPath("").isEmpty());
        QVERIFY(sanitizeArchiveEntryPath("/").isEmpty());
        QVERIFY(sanitizeArchiveEntryPath("C:").isEmpty());
        QVERIFY(sanitizeArchiveEntryPath("./").isEmpty());
    }

    // Dot-prefixed names are files, not traversal.
    void keepsDotFiles() {
        QCOMPARE(sanitizeArchiveEntryPath("Data\\.soleromarker"),
                 QStringLiteral("Data/.soleromarker"));
        QCOMPARE(sanitizeArchiveEntryPath("..foo/bar.esp"), QStringLiteral("..foo/bar.esp"));
    }
};

QTEST_MAIN(TestArchivePath)
#include "test_ArchivePath.moc"
