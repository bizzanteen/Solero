#include "ArchiveTool.h"
#include "ArchivePath.h"
#include "core/Log.h"
#include <algorithm>
#include <QProcess>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QRegularExpression>

namespace solero {

namespace {
// Forward an extractor's "...  84%" stdout chatter to onProgress. Both 7z
// (-bsp1) and unrar print percentages this way; parsing the latest match per
// chunk keeps the install dialog's bar moving on multi-GB archives.
void connectPercentProgress(QProcess& proc, const std::function<void(int)>& onProgress) {
    static const QRegularExpression rePct("(\\d+)%");
    QObject::connect(&proc, &QProcess::readyReadStandardOutput, &proc, [&proc, onProgress]{
        const QString chunk = QString::fromLatin1(proc.readAllStandardOutput());
        auto it = rePct.globalMatch(chunk);
        int last = -1;
        while (it.hasNext()) last = it.next().captured(1).toInt();
        if (last >= 0 && onProgress) onProgress(last);
    });
}
} // namespace

QString ArchiveTool::sevenZipBinary() {
    for (const QString& cand : {"7z", "7za", "7zr"}) {
        QString full = QStandardPaths::findExecutable(cand);
        if (!full.isEmpty()) return full;
    }
    return {};
}

bool ArchiveTool::sevenZipAvailable() {
    const QString bin = sevenZipBinary();
    if (bin.isEmpty()) qCWarning(lcInstall) << "7z not found on PATH (7z/7za/7zr)";
    return !bin.isEmpty();
}

QStringList ArchiveTool::listEntries(const QString& archivePath, bool* ok) {
    QStringList result;
    if (ok) *ok = false;

    if (archivePath.endsWith(".rar", Qt::CaseInsensitive)) {
        QProcess proc;
        proc.start("unrar", {"lb", archivePath});
        if (!proc.waitForFinished(120000)) { qCWarning(lcInstall) << "unrar list timed out for" << archivePath; return result; }
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) { qCWarning(lcInstall) << "unrar list failed (exit" << proc.exitCode() << ") for" << archivePath; return result; }
        const QString out = QString::fromUtf8(proc.readAllStandardOutput());
        for (const QString& line : out.split('\n')) {
            QString t = line.trimmed();
            if (t.isEmpty()) continue;
            t.replace('\\', '/');
            result.append(t);
        }
        if (ok) *ok = true;
        return result;
    }

    QString bin = sevenZipBinary();
    if (bin.isEmpty()) { qCWarning(lcInstall) << "7z unavailable - cannot list" << archivePath; return result; }

    QProcess proc;
    proc.start(bin, {"l", "-ba", "-slt", archivePath});
    if (!proc.waitForFinished(120000)) { qCWarning(lcInstall) << "7z list timed out for" << archivePath; return result; }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) { qCWarning(lcInstall) << "7z list failed (exit" << proc.exitCode() << ") for" << archivePath; return result; }

    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    QString curPath; bool curIsDir = false;
    auto flush = [&]{
        if (!curPath.isEmpty() && !curIsDir) {
            QString p = curPath; p.replace('\\', '/');
            result.append(p);
        }
        curPath.clear(); curIsDir = false;
    };
    for (const QString& line : out.split('\n')) {
        QString t = line.trimmed();
        if (t.isEmpty()) { flush(); continue; }
        if (t.startsWith("Path = "))            curPath = t.mid(7);
        else if (t.startsWith("Attributes = ")) curIsDir = t.mid(13).contains('D');
        else if (t.startsWith("Folder = "))      curIsDir = curIsDir || t.mid(9).startsWith('+');
    }
    flush();
    if (ok) *ok = true;
    return result;
}

QList<ArchiveTool::Entry> ArchiveTool::listEntriesWithCrc(const QString& archivePath, bool* ok) {
    QList<Entry> result;
    if (ok) *ok = false;

    // rar: fall back to the bare path list (no CRC available via unrar lb).
    if (archivePath.endsWith(".rar", Qt::CaseInsensitive)) {
        bool listed = false;
        const QStringList paths = listEntries(archivePath, &listed);
        if (!listed) return result;
        for (const QString& p : paths) result.append({ p, 0 });
        if (ok) *ok = true;
        return result;
    }

    QString bin = sevenZipBinary();
    if (bin.isEmpty()) return result;

    QProcess proc;
    proc.start(bin, {"l", "-ba", "-slt", archivePath});
    if (!proc.waitForFinished(120000)) return result;
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) return result;

    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    QString curPath; bool curIsDir = false; quint32 curCrc = 0;
    auto flush = [&]{
        if (!curPath.isEmpty() && !curIsDir) {
            QString p = curPath; p.replace('\\', '/');
            result.append({ p, curCrc });
        }
        curPath.clear(); curIsDir = false; curCrc = 0;
    };
    for (const QString& line : out.split('\n')) {
        QString t = line.trimmed();
        if (t.isEmpty()) { flush(); continue; }
        if (t.startsWith("Path = "))            curPath = t.mid(7);
        else if (t.startsWith("Attributes = ")) curIsDir = t.mid(13).contains('D');
        else if (t.startsWith("Folder = "))      curIsDir = curIsDir || t.mid(9).startsWith('+');
        else if (t.startsWith("CRC = "))         curCrc = t.mid(6).trimmed().toUInt(nullptr, 16);
    }
    flush();
    if (ok) *ok = true;
    return result;
}

bool ArchiveTool::expandWindowsPaths(const QString& rootDirPath, QStringList* expanded) {
    const QString root = QDir::cleanPath(rootDirPath);
    QDir rootDir(root);

    QStringList wrong; // collect first - moving during the walk isn't safe
    {
        QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString abs = it.next();
            if (rootDir.relativeFilePath(abs).contains('\\')) wrong << abs;
        }
    }
    if (wrong.isEmpty()) return true;

    bool ok = true;
    for (const QString& abs : wrong) {
        const QString rel  = rootDir.relativeFilePath(abs);
        const QString safe = sanitizeArchiveEntryPath(rel);
        if (safe.isEmpty()) {
            qCWarning(lcInstall) << "archive entry cannot be made safe, rejecting:" << rel;
            ok = false;
            continue;
        }
        const QString dst = root + "/" + safe;
        if (QDir::cleanPath(dst) == QDir::cleanPath(abs)) continue;
        QDir().mkpath(QFileInfo(dst).path());
        if (QFile::exists(dst)) QFile::remove(dst); // last write wins, as 7z does
        if (!QFile::rename(abs, dst)) {
            qCWarning(lcInstall) << "could not expand Windows path" << rel << "->" << safe;
            ok = false;
            continue;
        }
        if (expanded) *expanded << safe;
        qCDebug(lcInstall) << "expanded Windows path" << rel << "->" << safe;
    }

    // Deepest first, so a nested dir empties before its parent is checked.
    QStringList dirs;
    {
        QDirIterator it(root, QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString abs = it.next();
            if (QFileInfo(abs).fileName().contains('\\')) dirs << abs;
        }
    }
    std::sort(dirs.begin(), dirs.end(),
              [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const QString& d : dirs)
        if (QDir(d).isEmpty()) QDir(d).removeRecursively();

    return ok;
}

bool ArchiveTool::validateExtraction(const QString& destDir) {
    const QString rootCanon = QFileInfo(destDir).canonicalFilePath();
    if (rootCanon.isEmpty()) {
        qCWarning(lcInstall) << "cannot canonicalize destDir for validation:" << destDir;
        return false;
    }
    const QString prefix = rootCanon + '/';
    bool ok = true;
    QDirIterator it(destDir,
                    QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString entry = it.next();
        const QFileInfo fi = it.fileInfo();
        bool escapes = false;
        if (fi.isSymLink()) {
            // Resolve the link target (Qt returns an absolute path; it may not
            // exist or still contain "..", so clean rather than canonicalize).
            QString target = fi.symLinkTarget();
            if (target.isEmpty()) target = fi.absoluteFilePath();
            const QString clean = QDir::cleanPath(target);
            escapes = (clean != rootCanon && !clean.startsWith(prefix));
        } else {
            const QString canon = fi.canonicalFilePath();
            escapes = !canon.isEmpty() && canon != rootCanon && !canon.startsWith(prefix);
        }
        if (escapes) {
            qCWarning(lcInstall) << "extracted entry escapes destDir, rejecting:" << entry;
            if (fi.isSymLink()) QFile::remove(entry);
            ok = false;
        }
    }
    return ok;
}

bool ArchiveTool::finalizeExtract(const QString& destDir, bool extractOk) {
    if (!extractOk) return false;
    // Expand first: "..\evil" is only a traversal once the separators are real.
    if (expandWindowsPaths(destDir) && validateExtraction(destDir)) return true;
    qCWarning(lcInstall) << "extraction failed path-safety validation, removing" << destDir;
    QDir(destDir).removeRecursively();
    return false;
}

bool ArchiveTool::extract(const QString& archivePath, const QString& destDir,
                          const std::function<void(int)>& onProgress) {
    qCInfo(lcInstall) << "extract:" << archivePath << "->" << destDir;
    if (archivePath.endsWith(".rar", Qt::CaseInsensitive)) {
        QDir().mkpath(destDir);
        // unrar first: it prints whole-archive percentages, so the install
        // dialog shows real progress. unar (the old primary) is silent under
        // -q, which left the bar frozen at zero for multi-GB rars.
        QProcess proc;
        QEventLoop loop;
        connectPercentProgress(proc, onProgress);
        QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         &loop, &QEventLoop::quit);
        proc.start("unrar", {"x", "-o+", "-y", archivePath, destDir + "/"});
        if (proc.waitForStarted(15000)) {
            loop.exec();
            bool okRar = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
            if (!okRar) qCWarning(lcInstall) << "unrar extract failed (exit" << proc.exitCode() << ") for" << archivePath;
            return finalizeExtract(destDir, okRar);
        }
        // unrar not available; fall back to unar (no progress output).
        qCWarning(lcInstall) << "unrar unavailable, falling back to unar for" << archivePath;
        QProcess pr;
        QEventLoop loop2;
        QObject::connect(&pr, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         &loop2, &QEventLoop::quit);
        pr.start("unar", {"-q", "-D", "-f", "-o", destDir, archivePath});
        if (!pr.waitForStarted(15000)) { qCWarning(lcInstall) << "unar unavailable - cannot extract" << archivePath; return false; }
        loop2.exec();
        bool okUnar = pr.exitStatus() == QProcess::NormalExit && pr.exitCode() == 0;
        if (!okUnar) qCWarning(lcInstall) << "unar extract failed (exit" << pr.exitCode() << ") for" << archivePath;
        return finalizeExtract(destDir, okUnar);
    }

    QString bin = sevenZipBinary();
    if (bin.isEmpty()) { qCWarning(lcInstall) << "7z unavailable - cannot extract" << archivePath; return false; }
    QDir().mkpath(destDir);
    QProcess proc;
    QEventLoop loop;
    connectPercentProgress(proc, onProgress);
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    proc.start(bin, {"x", archivePath, "-o" + destDir, "-y", "-bsp1"});
    if (!proc.waitForStarted(15000)) { qCWarning(lcInstall) << "7z failed to start for" << archivePath; return false; }
    loop.exec();
    bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (!ok) qCWarning(lcInstall) << "7z extract failed (exit" << proc.exitCode() << ") for" << archivePath;
    return finalizeExtract(destDir, ok);
}

bool ArchiveTool::isSolid(const QString& archivePath) {
    if (archivePath.endsWith(".rar", Qt::CaseInsensitive)) return false;
    QString bin = sevenZipBinary();
    if (bin.isEmpty()) return false;
    QProcess proc;
    proc.start(bin, {"l", "-slt", archivePath});
    if (!proc.waitForFinished(120000)) return false;
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) return false;
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    for (const QString& line : out.split('\n')) {
        QString t = line.trimmed();
        if (t.startsWith("Solid = ") && t.contains('+')) return true;
    }
    return false;
}

bool ArchiveTool::extractPaths(const QString& archivePath, const QString& destDir,
                               const QStringList& paths, bool recursive,
                               const std::function<void(int)>& onProgress) {
    // rar goes through unrar: distro builds of 7z often ship without the
    // (unfree) RAR codec, so the 7z path below fails on every rar and forced a
    // full extract just to peek at fomod/. Each requested path becomes a
    // root-level and an any-depth mask, mirroring 7z's -r name matching.
    if (archivePath.endsWith(".rar", Qt::CaseInsensitive)) {
        qCInfo(lcInstall) << "extractPaths (unrar):" << paths << "from" << archivePath << "->" << destDir;
        QDir().mkpath(destDir);
        QStringList args{"x", "-o+", "-y", archivePath};
        for (const QString& p : paths) {
            args << p << p + "/*";
            if (recursive) args << "*/" + p + "/*";
        }
        args << destDir + "/";
        QProcess proc;
        QEventLoop loop;
        connectPercentProgress(proc, onProgress);
        QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         &loop, &QEventLoop::quit);
        proc.start("unrar", args);
        if (!proc.waitForStarted(15000)) { qCWarning(lcInstall) << "unrar failed to start for" << archivePath; return false; }
        loop.exec();
        // Non-zero also covers "no files matched" - the caller's full-extract
        // fallback handles that.
        bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
        if (!ok) qCWarning(lcInstall) << "unrar extractPaths failed (exit" << proc.exitCode() << ") for" << archivePath;
        return finalizeExtract(destDir, ok);
    }

    QString bin = sevenZipBinary();
    if (bin.isEmpty()) { qCWarning(lcInstall) << "7z unavailable - cannot extract paths from" << archivePath; return false; }
    qCInfo(lcInstall) << "extractPaths:" << paths << "from" << archivePath << "->" << destDir;
    QDir().mkpath(destDir);
    QProcess proc;
    QEventLoop loop;
    connectPercentProgress(proc, onProgress);
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    QStringList args{"x", archivePath, "-o" + destDir, "-y", "-bsp1"};
    args += paths;
    if (recursive) args << "-r";
    proc.start(bin, args);
    if (!proc.waitForStarted(15000)) { qCWarning(lcInstall) << "7z failed to start for" << archivePath; return false; }
    loop.exec();
    bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (!ok) qCWarning(lcInstall) << "7z extractPaths failed (exit" << proc.exitCode() << ") for" << archivePath;
    return finalizeExtract(destDir, ok);
}

} // namespace solero
