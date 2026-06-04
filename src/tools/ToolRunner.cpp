#include "ToolRunner.h"
#include "core/AppConfig.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

namespace solero {

QStringList ToolRunner::snapshotFiles(const QString& dir) {
    QStringList out;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next();
    return out;
}

ToolRunner::Result ToolRunner::run(const Executable& exe, const QString& gameDir,
                                   const QString& stagingRoot) {
    Result r;
    QString captureBase = gameDir + "/Data";
    QStringList before;
    bool capture = exe.isCapturingOutput;
    if (capture) before = snapshotFiles(captureBase);

    QProcess proc;
    if (!exe.workingDir.isEmpty()) proc.setWorkingDirectory(exe.workingDir);
    QStringList args = exe.arguments.split(' ', Qt::SkipEmptyParts);

    if (exe.runtime == RuntimeType::Native) {
        if (QFile::exists(exe.binaryPath))
            QFile(exe.binaryPath).setPermissions(QFile(exe.binaryPath).permissions()
                | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeUser);
        proc.start(exe.binaryPath, args);
    } else {
        // Windows tool via umu-run, reusing the Skyrim Proton prefix.
        QString protonDir = AppConfig::instance().detectProtonDir();
        if (protonDir.isEmpty()) {
            r.error = "Could not find a Proton install to run this tool.";
            return r;
        }
        QString prefix = exe.winePrefix; // <steamapps>/compatdata/489830
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("WINEPREFIX", prefix + "/pfx");
        env.insert("STEAM_COMPAT_DATA_PATH", prefix);
        env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", QDir::homePath() + "/.local/share/Steam");
        env.insert("GAMEID", "umu-489830");
        env.insert("STORE", "none");
        env.insert("PROTONPATH", protonDir);
        env.insert("PROTON_VERB", "waitforexitandrun");
        proc.setProcessEnvironment(env);
        QStringList pargs; pargs << exe.binaryPath; pargs += args;
        proc.start("umu-run", pargs);
    }
    if (!proc.waitForStarted(15000)) { r.error = "Failed to start: " + exe.binaryPath; return r; }
    r.launched = true;
    proc.waitForFinished(-1);

    if (capture) {
        QSet<QString> beforeSet(before.begin(), before.end());
        QString destBase = exe.outputModId.isEmpty()
            ? (gameDir + "/.solero-overwrite")
            : (stagingRoot + "/" + exe.outputModId + "/Data");
        for (const QString& f : snapshotFiles(captureBase)) {
            if (beforeSet.contains(f)) continue;
            QString relUnderData = QDir(captureBase).relativeFilePath(f);
            QString dst = destBase + "/" + relUnderData;
            QDir().mkpath(QFileInfo(dst).path());
            QFile::rename(f, dst);
        }
    }
    return r;
}

} // namespace solero
