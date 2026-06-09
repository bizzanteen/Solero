#include <QtTest>
#include "core/AppConfig.h"

using namespace solero;

class TestAppConfig : public QObject { Q_OBJECT
private slots:
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
