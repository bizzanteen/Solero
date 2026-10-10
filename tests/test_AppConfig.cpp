#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "core/AppConfig.h"

using namespace solero;

class TestAppConfig : public QObject { Q_OBJECT
private slots:
    // The mod-list / plugin-list header-state blobs round-trip through config.json
    // (stored base64) so manually-resized column widths survive a restart.
    void headerState_roundTrips() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit()); // configPath() resolves via $HOME

        // Bytes that include NUL and high bytes, to prove base64 survives them.
        const QByteArray modBlob("\x00\x01\xfe\xff mod-header \x7f", 24);
        const QByteArray pluginBlob("plugin\x00\x10state", 12);

        auto& cfg = AppConfig::instance();
        cfg.setModListHeaderState(modBlob);
        cfg.setPluginListHeaderState(pluginBlob);
        QVERIFY(cfg.save());

        // Wipe the in-memory copies, then reload from disk.
        cfg.setModListHeaderState(QByteArray());
        cfg.setPluginListHeaderState(QByteArray());
        QVERIFY(cfg.load());

        QCOMPARE(cfg.modListHeaderState(), modBlob);
        QCOMPARE(cfg.pluginListHeaderState(), pluginBlob);
    }

    // Theme options (mode / accent / font) round-trip through config.json.
    void themeOptions_roundTrip() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        auto& cfg = AppConfig::instance();
        // Defaults before anything is set.
        QCOMPARE(cfg.themeMode(), QString("system"));
        QVERIFY(cfg.accentColor().isEmpty());

        cfg.setThemeMode("dark");
        cfg.setAccentColor("#3daee9");
        cfg.setFontFamily("Noto Sans");
        cfg.setFontSize(11);
        QVERIFY(cfg.save());

        cfg.setThemeMode("system");
        cfg.setAccentColor("");
        cfg.setFontFamily("");
        cfg.setFontSize(0);
        QVERIFY(cfg.load());

        QCOMPARE(cfg.themeMode(), QString("dark"));
        QCOMPARE(cfg.accentColor(), QString("#3daee9"));
        QCOMPARE(cfg.fontFamily(), QString("Noto Sans"));
        QCOMPARE(cfg.fontSize(), 11);
    }

    void gameType_roundTrips() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());

        auto& cfg = AppConfig::instance();
        cfg.setGameType(AppConfig::oblivionRemasteredGame());
        QVERIFY(cfg.save());
        cfg.setGameType(AppConfig::skyrimSeGame());
        QVERIFY(cfg.load());
        QCOMPARE(cfg.gameType(), AppConfig::oblivionRemasteredGame());
    }

    void legacyConfigDefaultsToSkyrim() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        qputenv("HOME", home.path().toLocal8Bit());
        QVERIFY(QDir().mkpath(AppConfig::dataRoot()));
        QFile f(AppConfig::configPath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"gameDir\":\"/legacy/skyrim\"}");
        f.close();

        auto& cfg = AppConfig::instance();
        cfg.setGameType(AppConfig::oblivionRemasteredGame());
        QVERIFY(cfg.load());
        QCOMPARE(cfg.gameType(), AppConfig::skyrimSeGame());
    }

    void gameDescriptors() {
        QCOMPARE(AppConfig::gameInstallDirectory(AppConfig::skyrimSeGame()),
                 QString("Skyrim Special Edition"));
        QCOMPARE(AppConfig::gameExecutable(AppConfig::skyrimSeGame()), QString("SkyrimSE.exe"));
        QCOMPARE(AppConfig::gameInstallDirectory(AppConfig::oblivionRemasteredGame()),
                 QString("Oblivion Remastered"));
        QCOMPARE(AppConfig::gameExecutable(AppConfig::oblivionRemasteredGame()),
                 QString("OblivionRemastered/Binaries/Win64/OblivionRemastered-Win64-Shipping.exe"));
        QCOMPARE(AppConfig::gameInstallMarker(AppConfig::oblivionRemasteredGame()),
                 QString("OblivionRemastered/Content/Dev/ObvData/Data/Oblivion.esm"));
        QCOMPARE(AppConfig::gameDataRelativePath(AppConfig::oblivionRemasteredGame()),
                 QString("OblivionRemastered/Content/Dev/ObvData/Data"));
        QCOMPARE(AppConfig::gameExecutable("unknown"), QString("SkyrimSE.exe"));

        auto& cfg = AppConfig::instance();
        cfg.setGameDir("/games/oblivion");
        cfg.setGameType(AppConfig::oblivionRemasteredGame());
        QCOMPARE(cfg.dataDir(), QString("/games/oblivion/OblivionRemastered/Content/Dev/ObvData/Data"));
        cfg.setGameType(AppConfig::skyrimSeGame());
        QCOMPARE(cfg.dataDir(), QString("/games/oblivion/Data"));
    }

    // Modern nested libraryfolders.vdf form.
    void parseVdf_nestedForm() {
        const QString vdf = R"(
"libraryfolders"
{
    "0"
    {
        "path"		"/home/user/.local/share/Steam"
        "label"		""
        "apps" { "489830" "12345" }
    }
    "1"
    {
        "path"		"/run/media/user/Games/SteamLibrary"
    }
}
)";
        QStringList libs = AppConfig::parseLibraryFoldersVdf(vdf);
        QCOMPARE(libs.size(), 2);
        QVERIFY(libs.contains("/home/user/.local/share/Steam"));
        QVERIFY(libs.contains("/run/media/user/Games/SteamLibrary"));
    }

    // Legacy flat form (path keys at top level), plus dedupe.
    void parseVdf_flatFormAndDedupe() {
        const QString vdf = R"(
"LibraryFolders"
{
    "path"   "/lib/a"
    "path"   "/lib/b"
    "path"   "/lib/a"
}
)";
        QStringList libs = AppConfig::parseLibraryFoldersVdf(vdf);
        QCOMPARE(libs.size(), 2);
        QCOMPARE(libs.at(0), QString("/lib/a"));
        QCOMPARE(libs.at(1), QString("/lib/b"));
    }

    // Windows path with escaped backslashes is unescaped.
    void parseVdf_unescapesBackslashes() {
        const QString vdf = "\"path\"\t\"D:\\\\SteamLibrary\"";
        QStringList libs = AppConfig::parseLibraryFoldersVdf(vdf);
        QCOMPARE(libs.size(), 1);
        QCOMPARE(libs.at(0), QString("D:\\SteamLibrary"));
    }

    void parseVdf_empty() {
        QVERIFY(AppConfig::parseLibraryFoldersVdf("").isEmpty());
        QVERIFY(AppConfig::parseLibraryFoldersVdf("garbage no paths").isEmpty());
    }
};

QTEST_MAIN(TestAppConfig)
#include "test_AppConfig.moc"
