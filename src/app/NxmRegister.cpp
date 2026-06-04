#include "app/NxmRegister.h"
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QTextStream>
#include <QStandardPaths>

namespace solero {

// Force x-scheme-handler/nxm=solero.desktop in a mimeapps.list, overriding any
// conflicting default (e.g. a stale Fluorine/MO2 entry that Flatpak browsers read
// via the portal). Creates the file/section if missing.
static void ensureNxmDefault(const QString& path) {
    const QString line = QStringLiteral("x-scheme-handler/nxm=solero.desktop;");
    QStringList lines;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&in);
        while (!ts.atEnd()) lines << ts.readLine();
        in.close();
    }
    if (lines.isEmpty()) lines << QStringLiteral("[Default Applications]");

    bool replacedInDefaults = false;
    QString section;
    for (int i = 0; i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (t.startsWith('[') && t.endsWith(']')) { section = t; continue; }
        if (t.startsWith("x-scheme-handler/nxm=")) {
            lines[i] = line;
            if (section == QStringLiteral("[Default Applications]")) replacedInDefaults = true;
        }
    }
    if (!replacedInDefaults) {
        int idx = lines.indexOf(QStringLiteral("[Default Applications]"));
        if (idx < 0) { lines.prepend(QStringLiteral("[Default Applications]")); idx = 0; }
        lines.insert(idx + 1, line);
    }

    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream ts(&out);
        for (const QString& l : lines) ts << l << '\n';
    }
}

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

    // Override any conflicting nxm default that Flatpak browsers read via the
    // portal (e.g. a stale Fluorine/MO2 entry in the data-dir mimeapps.list).
    ensureNxmDefault(appsDir + "/mimeapps.list");
    ensureNxmDefault(QDir::homePath() + "/.config/mimeapps.list");

    // Best-effort: refresh the desktop + KDE mime caches (ignore failure).
    QProcess updateDb;
    updateDb.start("update-desktop-database", {appsDir});
    updateDb.waitForFinished();
    for (const QString& kb : {QStringLiteral("kbuildsycoca6"), QStringLiteral("kbuildsycoca5")}) {
        QProcess p; p.start(kb, {}); if (p.waitForStarted(1000)) { p.waitForFinished(5000); break; }
    }

    outMsg = QStringLiteral("Registered solero.desktop as the nxm:// handler (and cleared conflicting "
                            "defaults). Restart your browser for it to take effect.")
        ;
    return true;
}

} // namespace solero
