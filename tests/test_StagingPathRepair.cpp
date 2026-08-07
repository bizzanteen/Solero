// The one-time repair for mods that were staged before extraction expanded
// Windows path separators. Nobody re-installs a mod that reports no problem, so
// without this those mods stay silently inert forever.
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include "install/StagingPathRepair.h"
using namespace solero;

static void writeFile(const QString& path, const QByteArray& content) {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

static QStringList filesUnder(const QString& dir) {
    QStringList out;
    const QString norm = QDir::cleanPath(dir);
    QDirIterator it(norm, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next().mid(norm.length() + 1);
    out.sort();
    return out;
}

class TestStagingPathRepair : public QObject {
    Q_OBJECT
private slots:
    // Main Menu Randomizer SE as it sits on disk today.
    void expandsABackslashNamedFileInAModFolder() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/mods";
        writeFile(staging + "/Main Menu Randomizer SE/Data/SKSE\\Plugins\\main_menu_randomizer.dll",
                  "DLL");

        QStringList repaired;
        QCOMPARE(repairWindowsPathsInStaging(staging, &repaired), 1);
        QCOMPARE(filesUnder(staging + "/Main Menu Randomizer SE"),
                 (QStringList{"Data/SKSE/Plugins/main_menu_randomizer.dll"}));
        QCOMPARE(repaired, (QStringList{
            "Main Menu Randomizer SE/Data/SKSE/Plugins/main_menu_randomizer.dll"}));

        QFile f(staging + "/Main Menu Randomizer SE/Data/SKSE/Plugins/main_menu_randomizer.dll");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("DLL"));
    }

    // The overwhelming majority of mods are fine and must not be touched, and the
    // pass has to be safe to run again on an already-repaired staging tree.
    void leavesHealthyModsAloneAndIsIdempotent() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/mods";
        writeFile(staging + "/Good Mod/Data/meshes/a.nif", "A");
        writeFile(staging + "/Good Mod/Data/Skyrim Special Edition.esp", "B");
        writeFile(staging + "/Bad Mod/Data/SKSE\\Plugins\\x.dll", "C");

        QCOMPARE(repairWindowsPathsInStaging(staging), 1);
        QCOMPARE(repairWindowsPathsInStaging(staging), 0);
        QCOMPARE(filesUnder(staging + "/Good Mod"),
                 (QStringList{"Data/Skyrim Special Edition.esp", "Data/meshes/a.nif"}));
        QCOMPARE(filesUnder(staging + "/Bad Mod"), (QStringList{"Data/SKSE/Plugins/x.dll"}));
    }

    // Repair must never destroy a user's file, even one it cannot place safely.
    void leavesAnUnsafeNameWhereItIsRatherThanDeletingIt() {
        QTemporaryDir tmp;
        const QString staging = tmp.path() + "/mods";
        writeFile(staging + "/Odd Mod/..\\escape.dll", "E");

        QCOMPARE(repairWindowsPathsInStaging(staging), 0);
        QVERIFY2(QFile::exists(staging + "/Odd Mod/..\\escape.dll"),
                 "deleted a staged file it could not place");
    }

    void handlesAnEmptyOrMissingStagingDir() {
        QTemporaryDir tmp;
        QCOMPARE(repairWindowsPathsInStaging(tmp.path() + "/nope"), 0);
        QCOMPARE(repairWindowsPathsInStaging(QString()), 0);
        QCOMPARE(repairWindowsPathsInStaging(tmp.path()), 0);
    }
};

QTEST_MAIN(TestStagingPathRepair)
#include "test_StagingPathRepair.moc"
