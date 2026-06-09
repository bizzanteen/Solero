#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include "install/ModInstaller.h"
#include "install/ArchiveTool.h"
#include "core/StagingFolder.h"
#include "core/Types.h"
using namespace solero;

static void writeFile(const QString& path, const QByteArray& c = "x") {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path); f.open(QIODevice::WriteOnly); f.write(c);
}

class TestModInstaller : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (!ArchiveTool::sevenZipAvailable()) QSKIP("7z not available");
    }
    void install_dataRelative_wrapsUnderData() {
        QTemporaryDir tmp;
        QString src = tmp.path() + "/srcmod";
        writeFile(src + "/textures/sky.dds", "tex");
        QString archive = tmp.path() + "/CoolTextures.zip";
        QProcess z; z.setWorkingDirectory(src);
        z.start("7z", {"a", archive, "."});
        QVERIFY(z.waitForFinished(60000));

        QString staging = tmp.path() + "/staging";
        auto r = ModInstaller::installArchive(archive, staging);
        QVERIFY2(r.success, r.errorMessage.toUtf8());
        QCOMPARE(r.modName, QString("CoolTextures"));
        QVERIFY(QFile::exists(staging + "/" + r.modId + "/Data/textures/sky.dds"));
    }
    // Regression: a FOMOD whose contents are wrapped in a top-level folder
    // (like Skyland AIO: "Skyland AIO/fomod/...") must report a fomodBase that
    // is the parent of the `fomod` dir, not the raw extractDir. The wizard
    // resolves image paths (e.g. "fomod/screens/a.jpg") against fomodBase; if it
    // used extractDir it would look one level too shallow and show no images.
    void prepare_wrappedFomod_baseResolvesImages() {
        QTemporaryDir tmp;
        // Wrapper dir "Skylandish" mirrors Skyland's archive-root folder.
        QString src = tmp.path() + "/srcmod";
        writeFile(src + "/Skylandish/fomod/ModuleConfig.xml",
                  "<config><moduleName>X</moduleName></config>");
        writeFile(src + "/Skylandish/fomod/screens/a.jpg", "img");
        writeFile(src + "/Skylandish/textures/x.dds", "tex");
        QString archive = tmp.path() + "/Skylandish.7z";
        QProcess z; z.setWorkingDirectory(src);
        z.start("7z", {"a", archive, "."});
        QVERIFY(z.waitForFinished(60000));

        auto prep = ModInstaller::prepare(archive);
        QVERIFY2(prep.ok, prep.errorMessage.toUtf8());
        QVERIFY(prep.layout.isFomod);
        QVERIFY(!prep.fomodConfigPath.isEmpty());
        QVERIFY(!prep.fomodBase.isEmpty());
        // The image referenced as "fomod/screens/a.jpg" must resolve under
        // fomodBase (the wrapper dir), which is where the wizard will look.
        QVERIFY2(QFile::exists(prep.fomodBase + "/fomod/screens/a.jpg"),
                 ("fomodBase=" + prep.fomodBase).toUtf8());
        // And it is genuinely deeper than extractDir - proving the old
        // extractDir-based lookup (the bug) would have missed it.
        QVERIFY(!QFile::exists(prep.extractDir + "/fomod/screens/a.jpg"));
        QVERIFY(prep.fomodBase != prep.extractDir);
    }
    void install_gameRoot_noWrap() {
        QTemporaryDir tmp;
        QString src = tmp.path() + "/srcmod";
        writeFile(src + "/skse64_loader.exe", "exe");
        writeFile(src + "/Data/Scripts/a.pex", "pex");
        QString archive = tmp.path() + "/SKSE.7z";
        QProcess z; z.setWorkingDirectory(src);
        z.start("7z", {"a", archive, "."});
        QVERIFY(z.waitForFinished(60000));

        QString staging = tmp.path() + "/staging";
        auto r = ModInstaller::installArchive(archive, staging);
        QVERIFY(r.success);
        QVERIFY(QFile::exists(staging + "/" + r.modId + "/skse64_loader.exe"));
        QVERIFY(QFile::exists(staging + "/" + r.modId + "/Data/Scripts/a.pex"));
    }
    // Regression (data-loss): a Replace/Reinstall whose mod dir was migrated from
    // its UUID to a human staging-folder name must write into THAT folder (the one
    // deploy reads via stagingPathFor), not the UUID dir. We pass a
    // stagingFolderOverride resolved by stagingPathFor and assert files land there.
    void reinstall_writesIntoStagingFolder_notUuid() {
        QTemporaryDir tmp;
        QString src = tmp.path() + "/srcmod";
        writeFile(src + "/Data/textures/new.dds", "new");
        QString archive = tmp.path() + "/MyMod.zip";
        QProcess z; z.setWorkingDirectory(src);
        z.start("7z", {"a", archive, "."});
        QVERIFY(z.waitForFinished(60000));

        QString staging = tmp.path() + "/staging";

        // Simulate an existing mod whose dir was migrated to its human name.
        ModEntry existing;
        existing.id = "11111111-2222-3333-4444-555555555555";
        existing.name = "My Cool Mod";
        existing.stagingFolder = "My Cool Mod";       // migrated, != UUID
        const QString resolved = stagingPathFor(staging, existing); // staging/My Cool Mod
        QVERIFY(resolved.endsWith("/My Cool Mod"));

        auto prep = ModInstaller::prepare(archive);
        QVERIFY2(prep.ok, prep.errorMessage.toUtf8());
        auto r = ModInstaller::stageSimple(prep, staging, existing.id, {}, resolved);
        QVERIFY2(r.success, r.errorMessage.toUtf8());
        QCOMPARE(r.modId, existing.id);

        // Files land in the human-named staging folder (where deploy reads)…
        QVERIFY(QFile::exists(resolved + "/Data/textures/new.dds"));
        // …and not in the UUID dir (the old, orphaning bug).
        QVERIFY(!QDir(staging + "/" + existing.id).exists());
    }
};
QTEST_MAIN(TestModInstaller)
#include "test_ModInstaller.moc"
