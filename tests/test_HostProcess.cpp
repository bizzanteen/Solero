#include <QtTest>
#include "core/HostProcess.h"
using namespace solero;

class TestHostProcess : public QObject {
    Q_OBJECT
private slots:
    // Outside Flatpak, the command is unchanged and the caller applies env itself.
    void native_passesThrough() {
        const auto c = hostCommand("steam", {"-silent"}, {}, /*inFlatpak=*/false);
        QCOMPARE(c.program, QString("steam"));
        QCOMPARE(c.args, (QStringList{"-silent"}));
        QVERIFY(c.applyEnvToProcess);
    }

    // Inside Flatpak, wrap with flatpak-spawn --host; caller must not set sandbox env.
    void flatpak_wrapsWithHost() {
        const auto c = hostCommand("steam", {"-silent"}, {}, /*inFlatpak=*/true);
        QCOMPARE(c.program, QString("flatpak-spawn"));
        QCOMPARE(c.args, (QStringList{"--host", "steam", "-silent"}));
        QVERIFY(!c.applyEnvToProcess);
    }

    // Env overrides become --env=K=V flags (sorted by key, deterministic), placed
    // before the program, so the host process gets them on top of the host env.
    void flatpak_foldsEnvOverrides() {
        QMap<QString, QString> env;
        env["WINEPREFIX"] = "/p/pfx";
        env["PROTON_VERB"] = "waitforexitandrun";
        const auto c = hostCommand("umu-run", {"game.exe"}, env, /*inFlatpak=*/true);
        QCOMPARE(c.program, QString("flatpak-spawn"));
        QCOMPARE(c.args, (QStringList{"--host",
                                      "--env=PROTON_VERB=waitforexitandrun",
                                      "--env=WINEPREFIX=/p/pfx",
                                      "umu-run", "game.exe"}));
        QVERIFY(!c.applyEnvToProcess);
    }

    // Native run with env overrides: passed through for the caller to apply.
    void native_withEnv_appliesAtCaller() {
        QMap<QString, QString> env; env["WINEPREFIX"] = "/p";
        const auto c = hostCommand("umu-run", {"x"}, env, /*inFlatpak=*/false);
        QCOMPARE(c.program, QString("umu-run"));
        QCOMPARE(c.args, (QStringList{"x"}));
        QVERIFY(c.applyEnvToProcess);
    }
};
QTEST_APPLESS_MAIN(TestHostProcess)
#include "test_HostProcess.moc"
