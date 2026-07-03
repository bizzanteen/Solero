#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <filesystem>
#include "core/ShaderCache.h"
#include "core/ModList.h"
#include "core/Types.h"

using namespace solero;

namespace {
// Write `content` to a file at `path`, creating parent dirs.
void writeFile(const QString& path, const QByteArray& content = "x") {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
    f.close();
}
} // namespace

class TestShaderCache : public QObject {
    Q_OBJECT
private slots:

    // clearShaderCache removes only the three ShaderCache dirs and reports them,
    // leaving every other file/dir in the tree intact.
    void clear_removesOnlyShaderCacheDirs() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString root = tmp.path();
        const QString gameDir   = root + "/game";
        const QString overwrite = root + "/dataroot/overwrite/MyProfile"; // per-profile Overwrite
        const QString staging   = root + "/staging/cachemod";

        // ShaderCache files in all three locations.
        writeFile(gameDir   + "/Data/ShaderCache/a.bin", QByteArray(100, 'a'));
        writeFile(overwrite + "/ShaderCache/b.bin", QByteArray(50, 'b'));
        writeFile(staging   + "/Data/ShaderCache/c.bin", QByteArray(25, 'c'));

        // Bystanders that must survive.
        writeFile(gameDir   + "/Data/Skyrim.esm");
        writeFile(gameDir   + "/Data/Textures/x.dds");
        writeFile(overwrite + "/SKSE/foo.dll");
        writeFile(staging   + "/Data/meshes/y.nif");

        const ShaderCacheClearResult r = clearShaderCache(gameDir, overwrite, staging);

        // The three ShaderCache dirs are gone.
        QVERIFY(!QDir(gameDir   + "/Data/ShaderCache").exists());
        QVERIFY(!QDir(overwrite + "/ShaderCache").exists());
        QVERIFY(!QDir(staging   + "/Data/ShaderCache").exists());

        // Everything else survives.
        QVERIFY(QFile::exists(gameDir   + "/Data/Skyrim.esm"));
        QVERIFY(QFile::exists(gameDir   + "/Data/Textures/x.dds"));
        QVERIFY(QFile::exists(overwrite + "/SKSE/foo.dll"));
        QVERIFY(QFile::exists(staging   + "/Data/meshes/y.nif"));

        // Reports all three removals and the summed byte count.
        QCOMPARE(r.removedPaths.size(), 3);
        QCOMPARE(r.bytesRemoved, qint64(100 + 50 + 25));
    }

    // An empty cacheStagingDir skips the staging location (the other two still go).
    void clear_emptyStagingSkipped() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString gameDir   = tmp.path() + "/game";
        const QString overwrite = tmp.path() + "/dataroot/overwrite/MyProfile";
        writeFile(gameDir + "/Data/ShaderCache/a.bin");

        const ShaderCacheClearResult r = clearShaderCache(gameDir, overwrite, QString());
        QVERIFY(!QDir(gameDir + "/Data/ShaderCache").exists());
        QCOMPARE(r.removedPaths.size(), 1);
    }

    // captureShaderCache moves only files under Data/ShaderCache into staging; a
    // sibling new file outside ShaderCache is left in place. Files already present
    // in staging are not re-moved.
    void capture_movesOnlyShaderCacheFiles() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString gameDir = tmp.path() + "/game";
        const QString staging = tmp.path() + "/staging/cachemod";

        // New shader files in the live game dir.
        writeFile(gameDir + "/Data/ShaderCache/new1.bin");
        writeFile(gameDir + "/Data/ShaderCache/sub/new2.bin");
        // A non-cache sibling that must not be captured.
        writeFile(gameDir + "/Data/SKSE/runtime.log");
        // A shader file already captured in staging - must be left in the game dir.
        writeFile(gameDir + "/Data/ShaderCache/existing.bin", "live");
        writeFile(staging + "/Data/ShaderCache/existing.bin", "staged");

        QStringList movedRel;
        const int moved = captureShaderCache(gameDir, staging, &movedRel);
        QCOMPARE(moved, 2);

        // movedRelPaths records the moved files relative to gameDir (gameDir/Data
        // + rel), and does not list the already-staged immutable blob.
        movedRel.sort();
        QCOMPARE(movedRel, (QStringList{"Data/ShaderCache/new1.bin",
                                        "Data/ShaderCache/sub/new2.bin"}));

        // New shaders moved into staging and removed from the game dir.
        QVERIFY(QFile::exists(staging + "/Data/ShaderCache/new1.bin"));
        QVERIFY(QFile::exists(staging + "/Data/ShaderCache/sub/new2.bin"));
        QVERIFY(!QFile::exists(gameDir + "/Data/ShaderCache/new1.bin"));
        QVERIFY(!QFile::exists(gameDir + "/Data/ShaderCache/sub/new2.bin"));

        // Non-cache sibling untouched.
        QVERIFY(QFile::exists(gameDir + "/Data/SKSE/runtime.log"));

        // Already-staged file left alone in both places (not re-moved/clobbered).
        QVERIFY(QFile::exists(gameDir + "/Data/ShaderCache/existing.bin"));
        QFile staged(staging + "/Data/ShaderCache/existing.bin");
        QVERIFY(staged.open(QIODevice::ReadOnly));
        QCOMPARE(staged.readAll(), QByteArray("staged"));
    }

    // Info.ini is CS's disk-cache validation file (records per-feature enabled
    // state + version). CS rewrites it whenever feature state changes. Unlike the
    // immutable content-hashed shader blobs, a STALE staged Info.ini must be
    // refreshed on capture - otherwise deploy keeps restoring an out-of-date
    // validation file and CS invalidates + recompiles the whole cache on every
    // launch. So capture overwrites an existing staged Info.ini, while still
    // leaving already-captured blobs untouched.
    void capture_refreshesStaleInfoIni() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString gameDir = tmp.path() + "/game";
        const QString staging = tmp.path() + "/staging/cachemod";

        // Live (current) Info.ini written by CS this session.
        writeFile(gameDir + "/Data/ShaderCache/Info.ini", "current-feature-state");
        // Stale staged Info.ini from a previous capture.
        writeFile(staging + "/Data/ShaderCache/Info.ini", "stale-feature-state");
        // An already-captured immutable blob that must not be clobbered.
        writeFile(gameDir + "/Data/ShaderCache/Effect/abc.pso", "live-blob");
        writeFile(staging + "/Data/ShaderCache/Effect/abc.pso", "staged-blob");

        QStringList movedRel;
        const int moved = captureShaderCache(gameDir, staging, &movedRel);

        // Info.ini refreshed to the current state (the blob is not counted/moved).
        QCOMPARE(moved, 1);

        // The refreshed Info.ini is reported (gameDir-relative); the already-staged
        // immutable blob is not listed.
        QCOMPARE(movedRel, (QStringList{"Data/ShaderCache/Info.ini"}));
        QFile info(staging + "/Data/ShaderCache/Info.ini");
        QVERIFY(info.open(QIODevice::ReadOnly));
        QCOMPARE(info.readAll(), QByteArray("current-feature-state"));
        // Live Info.ini consumed (moved into staging).
        QVERIFY(!QFile::exists(gameDir + "/Data/ShaderCache/Info.ini"));

        // The immutable blob is left alone in both places.
        QVERIFY(QFile::exists(gameDir + "/Data/ShaderCache/Effect/abc.pso"));
        QFile blob(staging + "/Data/ShaderCache/Effect/abc.pso");
        QVERIFY(blob.open(QIODevice::ReadOnly));
        QCOMPARE(blob.readAll(), QByteArray("staged-blob"));
    }

    // Community Shaders DELETES the live Data/ShaderCache at runtime whenever it
    // invalidates the cache, removing the hardlinks Solero deployed. The deploy
    // record then reads "clean" while the staged Info.ini is no longer in the live
    // dir, so CS recompiles every launch. assertShaderCacheDeployed re-links any
    // staged file missing from the live dir (independent of the deploy-clean flag)
    // and leaves already-correctly-linked files untouched.
    void assert_relinksMissingCacheFilesIntoLiveDir() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString gameDir = tmp.path() + "/game";
        const QString staging = tmp.path() + "/staging/cachemod";

        // Staged managed cache: an Info.ini (CS's validation file) + a blob.
        writeFile(staging + "/Data/ShaderCache/Info.ini", "plugin=1-7-1-0");
        writeFile(staging + "/Data/ShaderCache/Effect/abc.pso", "blob");

        // Live game dir: the blob is already hard-linked in (same inode), but CS
        // deleted Info.ini this session - it's missing from the live dir.
        QDir().mkpath(gameDir + "/Data/ShaderCache/Effect");
        std::error_code ec;
        std::filesystem::create_hard_link(
            (staging + "/Data/ShaderCache/Effect/abc.pso").toStdString(),
            (gameDir + "/Data/ShaderCache/Effect/abc.pso").toStdString(), ec);
        QVERIFY(!ec);

        QStringList relinked;
        const int n = assertShaderCacheDeployed(gameDir, staging, /*hardlink=*/true, &relinked);

        // Only the missing Info.ini is restored; the already-linked blob is skipped.
        QCOMPARE(n, 1);
        QCOMPARE(relinked, (QStringList{"Data/ShaderCache/Info.ini"}));

        // Info.ini now lives in the game dir with the staged content...
        QFile info(gameDir + "/Data/ShaderCache/Info.ini");
        QVERIFY(info.open(QIODevice::ReadOnly));
        QCOMPARE(info.readAll(), QByteArray("plugin=1-7-1-0"));
        // ...and as a hardlink (same inode) to staging, so deploy stays in sync.
        QVERIFY(std::filesystem::equivalent(
            (staging + "/Data/ShaderCache/Info.ini").toStdString(),
            (gameDir + "/Data/ShaderCache/Info.ini").toStdString(), ec));
        QVERIFY(!ec);

        // The already-linked blob was left alone (still present, still linked).
        QVERIFY(QFile::exists(gameDir + "/Data/ShaderCache/Effect/abc.pso"));
    }

    // Empty staging or a missing staged ShaderCache dir -> no-op returning 0.
    void assert_noopWhenNothingStaged() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString gameDir = tmp.path() + "/game";
        QCOMPARE(assertShaderCacheDeployed(gameDir, QString(), true), 0);
        QCOMPARE(assertShaderCacheDeployed(gameDir, tmp.path() + "/staging", true), 0);
    }

    // Missing source / empty staging -> no-op returning 0.
    void capture_noopWhenMissing() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString gameDir = tmp.path() + "/game";
        // No ShaderCache dir at all.
        QCOMPARE(captureShaderCache(gameDir, tmp.path() + "/staging"), 0);
        // Empty staging dir -> skip.
        writeFile(gameDir + "/Data/ShaderCache/a.bin");
        QCOMPARE(captureShaderCache(gameDir, QString()), 0);
    }

    // activeCacheKey returns nexusFileId > normalised version > "default".
    void activeCacheKey_fallbackChain() {
        ModList ml;
        QCOMPARE(activeCacheKey(ml), QString("default")); // no CS mod

        ModEntry cs;
        cs.type = EntryType::Mod;
        cs.id   = "cs";
        cs.name = "Community Shaders";
        cs.version = "1.6.1";
        ml.append(cs);
        QCOMPARE(activeCacheKey(ml), QString("1.6.1")); // normalised version fallback

        ml.entries().last().nexusFileId = "f77";
        QCOMPARE(activeCacheKey(ml), QString("f77")); // fileId wins
    }

    // findCommunityShaders matches nexusModId 86492.
    void modList_findCommunityShaders() {
        ModList list;
        ModEntry plain;
        plain.type = EntryType::Mod;
        plain.id = "plain";
        plain.name = "Some Mod";
        list.append(plain);

        ModEntry cs;
        cs.type = EntryType::Mod;
        cs.id = "cs";
        cs.name = "Community Shaders";
        cs.nexusModId = "86492";
        list.append(cs);

        const ModEntry* found = list.findCommunityShaders();
        QVERIFY(found != nullptr);
        QCOMPARE(found->id, QString("cs"));
    }

    // findCommunityShaders also matches by name (case-insensitive) without a Nexus id.
    void modList_findCommunityShadersByName() {
        ModList list;
        ModEntry cs;
        cs.type = EntryType::Mod;
        cs.id = "cs";
        cs.name = "community shaders"; // lowercase
        list.append(cs);
        const ModEntry* found = list.findCommunityShaders();
        QVERIFY(found != nullptr);
        QCOMPARE(found->id, QString("cs"));
    }
};

QTEST_MAIN(TestShaderCache)
#include "test_ShaderCache.moc"
