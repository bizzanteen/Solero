#include "ToolRunner.h"
#include "core/AppConfig.h"
#include "deploy/DeployEngine.h"
#include "deploy/DeployRecord.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <QEventLoop>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>

namespace solero {

QStringList ToolRunner::tokenizeArgs(const QString& s) {
    QStringList out;
    QString cur;
    bool inSingle = false, inDouble = false, hasToken = false;
    for (int i = 0; i < s.size(); ++i) {
        QChar c = s[i];
        if (inSingle) {
            if (c == '\'') inSingle = false;
            else cur += c;
        } else if (inDouble) {
            if (c == '"') inDouble = false;
            else cur += c;
        } else if (c == '\'') {
            inSingle = true; hasToken = true;
        } else if (c == '"') {
            inDouble = true; hasToken = true;
        } else if (c.isSpace()) {
            if (hasToken) { out << cur; cur.clear(); hasToken = false; }
        } else {
            cur += c; hasToken = true;
        }
    }
    if (hasToken) out << cur;
    return out;
}

ToolRunner::Result ToolRunner::run(const Executable& exe, const QString& gameDir,
                                   const QString& stagingRoot) {
    Result r;
    QString captureBase = gameDir + "/Data";
    bool capture = exe.isCapturingOutput;

    // Record the launch time so we can capture any file created OR modified
    // during the run via a single post-run mtime walk (no giant before snapshot).
    QDateTime runStart;

    QProcess proc;
    // Many Windows tools (xEdit, DynDOLOD) expect cwd to be their install dir.
    proc.setWorkingDirectory(exe.workingDir.isEmpty()
                                 ? QFileInfo(exe.binaryPath).absolutePath()
                                 : exe.workingDir);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    QStringList args = tokenizeArgs(exe.arguments);

    // Wait for the process via an event loop so the GUI thread keeps pumping
    // events (no waitForFinished blocking -> app stays responsive). Connect
    // before starting so a fast-exiting process can't slip past the wait.
    QEventLoop loop;
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);

    if (exe.runtime == RuntimeType::Native) {
        if (exe.binaryPath.isEmpty() || !QFile::exists(exe.binaryPath)) {
            r.error = "Native binary not found: " + exe.binaryPath;
            return r;
        }
        QFile(exe.binaryPath).setPermissions(QFile(exe.binaryPath).permissions()
            | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeUser);
        if (capture) runStart = QDateTime::currentDateTime();
        proc.start(exe.binaryPath, args);
    } else {
        // Windows tool via umu-run, reusing the Skyrim Proton prefix.
        if (QStandardPaths::findExecutable("umu-run").isEmpty()) {
            r.error = "umu-run not found - install umu-launcher to run Windows tools.";
            return r;
        }
        QString protonDir = AppConfig::instance().detectProtonDir();
        if (protonDir.isEmpty()) {
            r.error = "Could not find a Proton install to run this tool.";
            return r;
        }
        QString prefix = exe.winePrefix; // <steamapps>/compatdata/489830
        // Derive the Steam root from the game dir (<steam>/steamapps/common/<Game>).
        QString steamRoot = QDir(gameDir + "/../../..").canonicalPath();
        if (steamRoot.isEmpty())
            steamRoot = QDir::homePath() + "/.local/share/Steam";
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("WINEPREFIX", prefix + "/pfx");
        env.insert("STEAM_COMPAT_DATA_PATH", prefix);
        env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamRoot);
        env.insert("GAMEID", "umu-489830");
        env.insert("STORE", "none");
        env.insert("PROTONPATH", protonDir);
        env.insert("PROTON_VERB", "waitforexitandrun");
        proc.setProcessEnvironment(env);
        QStringList pargs; pargs << exe.binaryPath; pargs += args;
        if (capture) runStart = QDateTime::currentDateTime();
        proc.start("umu-run", pargs);
    }
    if (!proc.waitForStarted(15000)) { r.error = "Failed to start: " + exe.binaryPath; return r; }
    r.launched = true;
    loop.exec();

    r.output = QString::fromUtf8(proc.readAll());
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        r.output += QString("\n[exit code %1]").arg(proc.exitCode());

    if (capture) {
        QString destBase = exe.outputModId.isEmpty()
            ? (AppConfig::dataRoot() + "/overwrite") // canonical Overwrite location
            : (stagingRoot + "/" + exe.outputModId + "/Data");
        // Skip files owned by the active deployment so a deployed mod file isn't
        // moved into the capture target merely because its mtime was bumped.
        DeployRecord rec = DeployRecord::loadFromFile(DeployEngine::recordPath(gameDir));
        QString warning;
        captureNewFiles(captureBase, destBase, gameDir, runStart, rec, &warning);
        r.output += warning;
    }
    return r;
}

int ToolRunner::captureNewFiles(const QString& captureBase, const QString& destBase,
                                const QString& gameDir, const QDateTime& runStart,
                                const DeployRecord& record, QString* warning) {
    QDir dataDir(captureBase);
    QDir gameRoot(gameDir);
    int moved = 0, failures = 0;
    // Single walk: capture every file touched at or after runStart. This catches
    // both newly created files (e.g. Community Shaders' runtime shader cache) and
    // in-place modifications (e.g. xEdit cleaning a master), which the old
    // before/after-set approach missed. Note: a file modified through a hardlink
    // also changes the staging master - a VFS-less limitation that's acceptable;
    // we still capture the result to the target.
    QDirIterator it(captureBase, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString f = it.next();
        if (it.fileInfo().lastModified() < runStart) continue;
        // relPath relative to gameDir (e.g. "Data/SKSE/Plugins/foo.dll") matches
        // the deploy record's keys; a managed/deployed file is left in place.
        if (!record.ownerOf(gameRoot.relativeFilePath(f)).isEmpty()) continue;
        QString relUnderData = dataDir.relativeFilePath(f);
        QString dst = destBase + "/" + relUnderData;
        QDir().mkpath(QFileInfo(dst).path());
        if (QFile::rename(f, dst)) { ++moved; continue; }
        // rename fails across filesystems -> copy then remove, and verify.
        if (QFile::exists(dst)) QFile::remove(dst);
        if (QFile::copy(f, dst) && QFile::remove(f)) { ++moved; continue; }
        ++failures;
    }
    if (failures > 0 && warning)
        *warning += QString("\n[warning: %1 captured file(s) could not be moved to the output mod]")
                        .arg(failures);
    return moved;
}

} // namespace solero
