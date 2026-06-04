#include "app/NxmRegister.h"
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QTextStream>
#include <QStandardPaths>

namespace solero {

bool NxmRegister::isRegistered() {
    QProcess proc;
    proc.start("xdg-mime", {"query", "default", "x-scheme-handler/nxm"});
    proc.waitForFinished();
    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    return out == QStringLiteral("solero.desktop");
}

bool NxmRegister::registerHandler(QString& outMsg) {
    const QString launcher = QDir::homePath() + "/.local/bin/solero";
    const QString appsDir = QDir::homePath() + "/.local/share/applications";
    const QString desktopPath = appsDir + "/solero.desktop";

    QDir().mkpath(appsDir);

    const QString contents = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Solero\n"
        "Comment=Mod Manager for Skyrim SE/AE\n"
        "Exec=%1 %u\n"
        "Icon=/var/home/eamon/dev/solero/resources/icons/solero-logo.svg\n"
        "Categories=Game;Utility;\n"
        "Keywords=mod;skyrim;modmanager;\n"
        "StartupWMClass=solero\n"
        "Terminal=false\n"
        "MimeType=x-scheme-handler/nxm;\n").arg(launcher);

    QFile f(desktopPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        outMsg = QStringLiteral("Could not write %1: %2").arg(desktopPath, f.errorString());
        return false;
    }
    {
        QTextStream ts(&f);
        ts << contents;
    }
    f.close();

    QProcess setDefault;
    setDefault.start("xdg-mime", {"default", "solero.desktop", "x-scheme-handler/nxm"});
    setDefault.waitForFinished();
    if (setDefault.exitStatus() != QProcess::NormalExit || setDefault.exitCode() != 0) {
        outMsg = QStringLiteral("xdg-mime default failed: %1")
            .arg(QString::fromUtf8(setDefault.readAllStandardError()).trimmed());
        return false;
    }

    // Best-effort: refresh the desktop database (ignore failure).
    QProcess updateDb;
    updateDb.start("update-desktop-database", {appsDir});
    updateDb.waitForFinished();

    outMsg = QStringLiteral("Wrote %1 and set it as the default x-scheme-handler/nxm handler.")
        .arg(desktopPath);
    return true;
}

} // namespace solero
