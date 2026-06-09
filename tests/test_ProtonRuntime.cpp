#include <QtTest>
#include "install/ProtonRuntime.h"
using namespace solero;

class TestProtonRuntime : public QObject {
    Q_OBJECT
private slots:
    // The real off-site requirement from SSE Engine Fixes (a learn.microsoft.com
    // vc-redist link with empty name/notes) -> recognised as VC++.
    void vcRedist_fromUrl() {
        QCOMPARE(protonProvidedRuntime(
                     "https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170",
                     "", ""),
                 QStringLiteral("Microsoft Visual C++ Redistributable"));
    }
    void vcRedist_fromName() {
        QCOMPARE(protonProvidedRuntime("https://aka.ms/vs/17/release/vc_redist.x64.exe", "", ""),
                 QStringLiteral("Microsoft Visual C++ Redistributable"));
        QCOMPARE(protonProvidedRuntime("", "Visual C++ 2015-2022 Redistributable", ""),
                 QStringLiteral("Microsoft Visual C++ Redistributable"));
    }
    void dotnet() {
        QCOMPARE(protonProvidedRuntime("https://dotnet.microsoft.com/download/dotnet/8.0", "", ""),
                 QStringLiteral("Microsoft .NET Runtime"));
        QCOMPARE(protonProvidedRuntime("", ".NET Framework 4.8", ""),
                 QStringLiteral("Microsoft .NET Runtime"));
    }
    void xna() {
        QCOMPARE(protonProvidedRuntime("", "Microsoft XNA Framework Redistributable 4.0", ""),
                 QStringLiteral("Microsoft XNA Framework"));
    }
    void directx() {
        QCOMPARE(protonProvidedRuntime("https://www.microsoft.com/.../directx_Jun2010_redist.exe", "", ""),
                 QStringLiteral("DirectX Runtime"));
    }
    // A genuine off-Nexus mod/patch (not a runtime) must not be classified.
    void notARuntime() {
        QVERIFY(protonProvidedRuntime("https://www.patreon.com/some-mod-author", "Cool Patch", "").isEmpty());
        QVERIFY(protonProvidedRuntime("https://github.com/foo/SomeTool", "Some Tool", "").isEmpty());
        QVERIFY(protonProvidedRuntime("", "", "").isEmpty());
    }
};

QTEST_MAIN(TestProtonRuntime)
#include "test_ProtonRuntime.moc"
