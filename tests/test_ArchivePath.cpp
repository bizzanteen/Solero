// Archive entry names come from whatever wrote the archive, so they can carry
// Windows separators, drive letters and traversal segments. Everything that
// reaches the filesystem goes through sanitizeArchiveEntryPath first.
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

    // The Main Menu Randomizer case: one entry name, three path components.
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

    // A name that survives normalisation but points at nothing can't be written.
    void rejectsAnEntryThatNormalisesToNothing() {
        QVERIFY(sanitizeArchiveEntryPath("").isEmpty());
        QVERIFY(sanitizeArchiveEntryPath("/").isEmpty());
        QVERIFY(sanitizeArchiveEntryPath("C:").isEmpty());
        QVERIFY(sanitizeArchiveEntryPath("./").isEmpty());
    }

    // A dot-prefixed name is a normal file, not a traversal segment.
    void keepsDotFiles() {
        QCOMPARE(sanitizeArchiveEntryPath("Data\\.soleromarker"),
                 QStringLiteral("Data/.soleromarker"));
        QCOMPARE(sanitizeArchiveEntryPath("..foo/bar.esp"), QStringLiteral("..foo/bar.esp"));
    }
};

QTEST_MAIN(TestArchivePath)
#include "test_ArchivePath.moc"
