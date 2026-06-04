#include "ArchiveTool.h"
#include <QProcess>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QEventLoop>
#include <QRegularExpression>

namespace solero {

QString ArchiveTool::sevenZipBinary() {
    for (const QString& cand : {"7z", "7za", "7zr"}) {
        QString full = QStandardPaths::findExecutable(cand);
        if (!full.isEmpty()) return full;
    }
    return {};
}

bool ArchiveTool::sevenZipAvailable() { return !sevenZipBinary().isEmpty(); }

QStringList ArchiveTool::listEntries(const QString& archivePath, bool* ok) {
    QStringList result;
    if (ok) *ok = false;

    if (archivePath.endsWith(".rar", Qt::CaseInsensitive)) {
        QProcess proc;
        proc.start("unrar", {"lb", archivePath});
        if (!proc.waitForFinished(120000)) return result;
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) return result;
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
    if (bin.isEmpty()) return result;

    QProcess proc;
    proc.start(bin, {"l", "-ba", "-slt", archivePath});
    if (!proc.waitForFinished(120000)) return result;
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) return result;

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

bool ArchiveTool::extract(const QString& archivePath, const QString& destDir,
                          const std::function<void(int)>& onProgress) {
    if (archivePath.endsWith(".rar", Qt::CaseInsensitive)) {
        QDir().mkpath(destDir);
        QProcess proc;
        QEventLoop loop;
        QObject::connect(&proc, &QProcess::readyReadStandardOutput, &proc, [&]{
            proc.readAllStandardOutput();
            if (onProgress) onProgress(-1); // indeterminate
        });
        QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         &loop, &QEventLoop::quit);
        proc.start("unar", {"-q", "-D", "-f", "-o", destDir, archivePath});
        if (!proc.waitForStarted(15000)) {
            // unar not available; fall back to unrar.
            QProcess pr;
            QEventLoop loop2;
            QObject::connect(&pr, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                             &loop2, &QEventLoop::quit);
            pr.start("unrar", {"x", "-o+", "-y", archivePath, destDir + "/"});
            if (!pr.waitForStarted(15000)) return false;
            loop2.exec();
            return pr.exitStatus() == QProcess::NormalExit && pr.exitCode() == 0;
        }
        loop.exec();
        return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    }

    QString bin = sevenZipBinary();
    if (bin.isEmpty()) return false;
    QDir().mkpath(destDir);
    QProcess proc;
    QEventLoop loop;
    static const QRegularExpression rePct("(\\d+)%");
    QObject::connect(&proc, &QProcess::readyReadStandardOutput, &proc, [&]{
        QString chunk = QString::fromLatin1(proc.readAllStandardOutput());
        auto it = rePct.globalMatch(chunk);
        int last = -1;
        while (it.hasNext()) last = it.next().captured(1).toInt();
        if (last >= 0 && onProgress) onProgress(last);
    });
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    proc.start(bin, {"x", archivePath, "-o" + destDir, "-y", "-bsp1"});
    if (!proc.waitForStarted(15000)) return false;
    loop.exec();
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
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
    QString bin = sevenZipBinary();
    if (bin.isEmpty()) return false;
    QDir().mkpath(destDir);
    QProcess proc;
    QEventLoop loop;
    static const QRegularExpression rePct("(\\d+)%");
    QObject::connect(&proc, &QProcess::readyReadStandardOutput, &proc, [&]{
        QString chunk = QString::fromLatin1(proc.readAllStandardOutput());
        auto it = rePct.globalMatch(chunk);
        int last = -1;
        while (it.hasNext()) last = it.next().captured(1).toInt();
        if (last >= 0 && onProgress) onProgress(last);
    });
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    QStringList args{"x", archivePath, "-o" + destDir, "-y", "-bsp1"};
    args += paths;
    if (recursive) args << "-r";
    proc.start(bin, args);
    if (!proc.waitForStarted(15000)) return false;
    loop.exec();
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

} // namespace solero
