#include <QtTest>
#include "install/DataDirDetector.h"
using namespace solero;

class TestDataDirDetector : public QObject {
    Q_OBJECT
private slots:
    void skse_gameRoot_noWrap() {
        QStringList files = {
            "skse64_loader.exe", "skse64_1_6_1170.dll",
            "Data/Scripts/actor.pex"
        };
        auto l = DataDirDetector::detect(files);
        QCOMPARE(l.stripComponents, 0);
        QCOMPARE(l.wrapInData, false);
        QCOMPARE(l.isFomod, false);
    }
    void dataRelative_textures_wrap() {
        QStringList files = { "textures/armor/iron.dds", "textures/sky.dds" };
        auto l = DataDirDetector::detect(files);
        QCOMPARE(l.stripComponents, 0);
        QCOMPARE(l.wrapInData, true);
    }
    void dataRelative_sksePlugin_wrap() {
        QStringList files = { "SKSE/Plugins/SSEDisplayTweaks.dll", "SKSE/Plugins/SSEDisplayTweaks.ini" };
        auto l = DataDirDetector::detect(files);
        QCOMPARE(l.wrapInData, true);
    }
    void wrapperDir_stripped() {
        QStringList files = { "MyMod/textures/a.dds", "MyMod/meshes/b.nif" };
        auto l = DataDirDetector::detect(files);
        QCOMPARE(l.stripComponents, 1);
        QCOMPARE(l.wrapInData, true);
    }
    void plugin_at_root_wrap() {
        QStringList files = { "MyMod.esp", "textures/a.dds" };
        auto l = DataDirDetector::detect(files);
        QCOMPARE(l.stripComponents, 0);
        QCOMPARE(l.wrapInData, true);
    }
    void fomod_detected() {
        QStringList files = { "fomod/ModuleConfig.xml", "fomod/info.xml", "Core/textures/a.dds" };
        auto l = DataDirDetector::detect(files);
        QCOMPARE(l.isFomod, true);
        QCOMPARE(l.fomodRootLevel, 0);
    }
    void fomod_withWrapper() {
        QStringList files = { "MyMod/fomod/ModuleConfig.xml", "MyMod/Core/a.esp" };
        auto l = DataDirDetector::detect(files);
        QCOMPARE(l.isFomod, true);
        QCOMPARE(l.fomodRootLevel, 1);
    }
};
QTEST_MAIN(TestDataDirDetector)
#include "test_DataDirDetector.moc"
