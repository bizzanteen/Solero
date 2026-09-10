#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include "install/ModInstaller.h"
#include "install/ArchiveTool.h"
#include "core/StagingFolder.h"
#include "core/Types.h"
#include "fomod/FomodTypes.h"
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

    // Helper: build a one-file archive and return its path.
    static QString makeArchive(QTemporaryDir& tmp, const QString& relFile) {
        QString src = tmp.path() + "/srcmod";
        writeFile(src + "/" + relFile, "a");
        QString archive = tmp.path() + "/M.zip";
        QProcess z; z.setWorkingDirectory(src);
        z.start("7z", {"a", archive, "."});
        z.waitForFinished(60000);
        return archive;
    }

    // a FOMOD `source` with a ".." traversal must be rejected - no read
    // outside fomodBase, and the install reports failure.
    void installOptionFiles_rejectsSourceTraversal() {
        QTemporaryDir tmp;
        QString archive = makeArchive(tmp, "real/a.txt");
        QString modDir = tmp.path() + "/mod"; QDir().mkpath(modDir);
        FomodFile f; f.source = "../../../etc/passwd"; f.destination = "passwd";
        QVERIFY(!ModInstaller::installOptionFiles(archive, modDir, {f}));
        QVERIFY(!QFile::exists(modDir + "/Data/passwd"));
    }

    // a FOMOD `destination` with a ".." traversal must be rejected - no
    // write outside the mod dir, and the install reports failure.
    void installOptionFiles_rejectsDestTraversal() {
        QTemporaryDir tmp;
        QString archive = makeArchive(tmp, "real/a.txt");
        QString modDir = tmp.path() + "/mod"; QDir().mkpath(modDir);
        FomodFile f; f.source = "real/a.txt"; f.destination = "../../escape.txt";
        QVERIFY(!ModInstaller::installOptionFiles(archive, modDir, {f}));
        QVERIFY(!QFile::exists(tmp.path() + "/escape.txt"));
    }

    // a missing/failed source must propagate as failure (not silent
    // success), while a valid sibling entry still installs.
    void installOptionFiles_missingSourcePropagatesFailure() {
        QTemporaryDir tmp;
        QString archive = makeArchive(tmp, "real/a.txt");
        QString modDir = tmp.path() + "/mod"; QDir().mkpath(modDir);
        FomodFile good;    good.source = "real/a.txt";    good.destination = "a.txt";
        FomodFile missing; missing.source = "real/nope.txt"; missing.destination = "nope.txt";
        QVERIFY(!ModInstaller::installOptionFiles(archive, modDir, {good, missing}));
        QVERIFY(QFile::exists(modDir + "/Data/a.txt"));   // successful path preserved
        QVERIFY(!QFile::exists(modDir + "/Data/nope.txt"));
    }
    // Regression: a FOMOD <file> with an EMPTY destination means the Data root,
    // not the source path. eFPS - Official Patch Hub sorts its patches into
    // category folders and flattens them this way; installing them at the source
    // path buried the plugins in Data/Cotn_serie/, where Skyrim never loads them.
    // An OMITTED destination still means "same as source" - FomodEngine resolves
    // that to the source at parse time, so an empty string here can only be the
    // explicit destination="" case.
    void stageFomod_emptyDestination_landsAtDataRoot() {
        QTemporaryDir tmp;
        QString src = tmp.path() + "/srcmod";
        writeFile(src + "/fomod/ModuleConfig.xml", "<config><moduleName>P</moduleName></config>");
        writeFile(src + "/Cotn_serie/Occ_COTN-Dawnstar.esp", "esp");
        writeFile(src + "/Nested/Deep/tex.dds", "tex");
        QString archive = tmp.path() + "/PatchHub.7z";
        QProcess z; z.setWorkingDirectory(src);
        z.start("7z", {"a", archive, "."});
        QVERIFY(z.waitForFinished(60000));

        auto prep = ModInstaller::prepare(archive);
        QVERIFY2(prep.ok, prep.errorMessage.toUtf8());

        FomodFile flat;  flat.source = "Cotn_serie/Occ_COTN-Dawnstar.esp"; flat.destination = "";
        FomodFile kept;  kept.source = "Nested/Deep/tex.dds"; kept.destination = "Nested/Deep/tex.dds";
        QString staging = tmp.path() + "/staging";
        auto r = ModInstaller::stageFomod(prep, staging, {flat, kept});
        QVERIFY2(r.success, r.errorMessage.toUtf8());
        QString mod = staging + "/" + r.modId;
        // empty destination -> Data root, basename only
        QVERIFY2(QFile::exists(mod + "/Data/Occ_COTN-Dawnstar.esp"),
                 "empty destination must flatten to the Data root");
        QVERIFY2(!QFile::exists(mod + "/Data/Cotn_serie/Occ_COTN-Dawnstar.esp"),
                 "empty destination must not fall back to the source path");
        // an explicit destination is still honoured verbatim
        QVERIFY(QFile::exists(mod + "/Data/Nested/Deep/tex.dds"));
    }
    // The spec default (destination attribute absent) must keep meaning "same as
    // source". FomodEngine hands us the source in that case, so it arrives here as
    // a non-empty destination and must not be flattened.
    void stageFomod_omittedDestination_keepsSourcePath() {
        QTemporaryDir tmp;
        QString src = tmp.path() + "/srcmod";
        writeFile(src + "/fomod/ModuleConfig.xml", "<config><moduleName>P</moduleName></config>");
        writeFile(src + "/meshes/rock/a.nif", "nif");
        QString archive = tmp.path() + "/Keep.7z";
        QProcess z; z.setWorkingDirectory(src);
        z.start("7z", {"a", archive, "."});
        QVERIFY(z.waitForFinished(60000));

        auto prep = ModInstaller::prepare(archive);
        QVERIFY2(prep.ok, prep.errorMessage.toUtf8());
        FomodFile f; f.source = "meshes/rock/a.nif"; f.destination = "meshes/rock/a.nif";
        QString staging = tmp.path() + "/staging";
        auto r = ModInstaller::stageFomod(prep, staging, {f});
        QVERIFY2(r.success, r.errorMessage.toUtf8());
        QVERIFY(QFile::exists(staging + "/" + r.modId + "/Data/meshes/rock/a.nif"));
    }
};
QTEST_MAIN(TestModInstaller)
#include "test_ModInstaller.moc"
