#include "ToolRunner.h"
#include "core/AppConfig.h"
#include "core/Log.h"
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
                                   const QString& stagingRoot,
                                   const QString& outputModFolder,
                                   const QString& overwriteDir) {
    Result r;
    bool capture = exe.isCapturingOutput;

    // Capture roots: always Data, plus any extra roots a tool/preset declares
    // (e.g. DynDOLOD_Output, xEdit root logs) relative to gameDir. Empty extras =
    // identical behavior to the old Data-only capture.
    QStringList captureBases;
    captureBases << gameDir + "/Data";
    for (const QString& extra : exe.captureRoots) {
        const QString trimmed = extra.trimmed();
        if (trimmed.isEmpty()) continue;
        // Resolve relative roots against gameDir; allow absolute as-is.
        const QString base = QFileInfo(trimmed).isAbsolute()
            ? trimmed : (gameDir + "/" + trimmed);
        if (!captureBases.contains(base)) captureBases << base;
    }

    // Record the launch time so we can capture any file created OR modified
    // during the run via a single post-run mtime walk (no giant before snapshot).
    QDateTime runStart;
    // Pre-launch mtime snapshot per capture base: lets the post-run walk tell a
    // genuinely new/modified file from a pre-existing unmanaged loose file that
    // merely shares the launch whole-second (see captureNewFiles).
    QHash<QString, QHash<QString, qint64>> preSnapshots;

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

    const QString exeName = QFileInfo(exe.binaryPath).fileName();
    if (exe.runtime == RuntimeType::Native) {
        if (exe.binaryPath.isEmpty() || !QFile::exists(exe.binaryPath)) {
            r.error = "Native binary not found: " + exe.binaryPath;
            qCWarning(lcTools) << "run:" << exeName << r.error;
            return r;
        }
        QFile(exe.binaryPath).setPermissions(QFile(exe.binaryPath).permissions()
            | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeUser);
        if (capture) {
            for (const QString& base : captureBases)
                preSnapshots.insert(base, snapshotMtimes(base));
            runStart = QDateTime::currentDateTime();
        }
        qCInfo(lcTools) << "launch native" << exeName << "cwd" << proc.workingDirectory()
                        << (capture ? "(capturing output)" : "");
        proc.start(exe.binaryPath, args);
    } else {
        // Windows tool via umu-run, reusing the Skyrim Proton prefix.
        if (QStandardPaths::findExecutable("umu-run").isEmpty()) {
            r.error = "umu-run not found - install umu-launcher to run Windows tools.";
            qCWarning(lcTools) << "run:" << exeName << r.error;
            return r;
        }
        QString protonDir = AppConfig::instance().detectProtonDir();
        if (protonDir.isEmpty()) {
            r.error = "Could not find a Proton install to run this tool.";
            qCWarning(lcTools) << "run:" << exeName << r.error;
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
        if (capture) {
            for (const QString& base : captureBases)
                preSnapshots.insert(base, snapshotMtimes(base));
            runStart = QDateTime::currentDateTime();
        }
        qCInfo(lcTools) << "launch proton" << exeName << "prefix" << prefix
                        << "cwd" << proc.workingDirectory() << (capture ? "(capturing output)" : "");
        proc.start("umu-run", pargs);
    }
    if (!proc.waitForStarted(15000)) {
        r.error = "Failed to start: " + exe.binaryPath;
        qCWarning(lcTools) << "run:" << exeName << "failed to start within 15s";
        return r;
    }
    r.launched = true;
    loop.exec();

    r.output = QString::fromUtf8(proc.readAll());
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        r.output += QString("\n[exit code %1]").arg(proc.exitCode());
        qCWarning(lcTools) << "run:" << exeName
                           << (proc.exitStatus() == QProcess::CrashExit ? "crashed" : "exited non-zero")
                           << "exitCode" << proc.exitCode();
    } else {
        qCInfo(lcTools) << "finished" << exeName << "exitCode" << proc.exitCode();
    }

    if (capture) {
        // Resolve the output mod's on-disk staging folder: the caller-provided
        // (name-based) folder when known, else the id as a fallback.
        const QString outFolder = outputModFolder.isEmpty() ? exe.outputModId
                                                            : outputModFolder;
        QString destBase = exe.outputModId.isEmpty()
            ? (overwriteDir.isEmpty() ? (AppConfig::dataRoot() + "/overwrite") // legacy fallback
                                      : overwriteDir)                          // per-profile Overwrite
            : (stagingRoot + "/" + outFolder + "/Data");
        // Skip files owned by the active deployment so a deployed mod file isn't
        // moved into the capture target merely because its mtime was bumped.
        DeployRecord rec = DeployRecord::loadFromFile(DeployEngine::recordPath(gameDir));
        QString warning;
        for (const QString& base : captureBases)
            captureNewFiles(base, destBase, gameDir, runStart, rec,
                            preSnapshots.value(base), &warning);
        r.output += warning;
    }
    return r;
}

QHash<QString, qint64> ToolRunner::snapshotMtimes(const QString& captureBase) {
    QHash<QString, qint64> out;
    QDirIterator it(captureBase, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        out.insert(f, it.fileInfo().lastModified().toMSecsSinceEpoch());
    }
    return out;
}

int ToolRunner::captureNewFiles(const QString& captureBase, const QString& destBase,
                                const QString& gameDir, const QDateTime& runStart,
                                const DeployRecord& record,
                                const QHash<QString, qint64>& preSnapshot,
                                QString* warning) {
    QDir dataDir(captureBase);
    QDir gameRoot(gameDir);
    int moved = 0, failures = 0;
    // Floor runStart to whole seconds: runStart has ms precision but many
    // filesystems store mtimes at whole-second granularity, so a file written at
    // 10.8s gets mtime 10.0s, which would compare < a runStart of 10.5s and be
    // skipped. Widening the window is safe - the deploy-record owner check below
    // still prevents re-capturing pre-existing managed files.
    const QDateTime cutoff = runStart.addMSecs(-runStart.time().msec());
    // Single walk: capture every file touched at or after runStart. This catches
    // both newly created files (e.g. Community Shaders' runtime shader cache) and
    // in-place modifications (e.g. xEdit cleaning a master), which the old
    // before/after-set approach missed. Note: a file modified through a hardlink
    // also changes the staging master - a VFS-less limitation that's acceptable;
    // we still capture the result to the target.
    QDirIterator it(captureBase, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString f = it.next();
        const qint64 mtime = it.fileInfo().lastModified().toMSecsSinceEpoch();
        if (it.fileInfo().lastModified() < cutoff) continue;
        // Whole-second floor can sweep up an UNMANAGED pre-existing loose file
        // written in the same second before launch. Guard with the pre-launch
        // snapshot: capture only if the file is genuinely new (absent from the
        // snapshot) OR its mtime is strictly greater than its snapshot value.
        auto snapIt = preSnapshot.constFind(f);
        if (snapIt != preSnapshot.constEnd() && mtime <= snapIt.value()) continue;
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
    if (failures > 0)
        qCWarning(lcTools) << "captureNewFiles:" << failures << "file(s) could not be moved to" << destBase;
    qCDebug(lcTools) << "captureNewFiles: moved" << moved << "file(s) from" << captureBase << "to" << destBase;
    return moved;
}

} // namespace solero
