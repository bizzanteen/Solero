#include "ToolRunner.h"
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
    QStringList before;
    bool capture = exe.isCapturingOutput;
    if (capture) before = snapshotFiles(gameDir);

    QProcess proc;
    if (!exe.workingDir.isEmpty()) proc.setWorkingDirectory(exe.workingDir);
    QStringList args = exe.arguments.split(' ', Qt::SkipEmptyParts);

    if (exe.runtime == RuntimeType::Native) {
        proc.start(exe.binaryPath, args);
    } else {
        // Proton: invoke the proton wrapper with the prefix env.
        QString proton = QDir(QDir::homePath() + "/.steam/root/compatibilitytools.d/"
                              + exe.protonVersion + "/proton").path();
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("STEAM_COMPAT_DATA_PATH", exe.winePrefix);
        env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", QDir::homePath() + "/.steam/root");
        proc.setProcessEnvironment(env);
        QStringList pargs; pargs << "run" << exe.binaryPath; pargs += args;
        proc.start(proton, pargs);
    }
    if (!proc.waitForStarted(15000)) { r.error = "Failed to start: " + exe.binaryPath; return r; }
    r.launched = true;
    proc.waitForFinished(-1);

    if (capture) {
        QSet<QString> beforeSet(before.begin(), before.end());
        QString destBase = exe.outputModId.isEmpty()
            ? (gameDir + "/.solero-overwrite")
            : (stagingRoot + "/" + exe.outputModId + "/Data");
        for (const QString& f : snapshotFiles(gameDir)) {
            if (beforeSet.contains(f)) continue;
            QString rel = QDir(gameDir).relativeFilePath(f);
            // strip leading "Data/" so it lands under the mod's Data/
            QString relUnderData = rel;
            if (relUnderData.startsWith("Data/", Qt::CaseInsensitive)) relUnderData = relUnderData.mid(5);
            QString dst = destBase + "/" + relUnderData;
            QDir().mkpath(QFileInfo(dst).path());
            QFile::rename(f, dst);
        }
    }
    return r;
}

} // namespace solero
