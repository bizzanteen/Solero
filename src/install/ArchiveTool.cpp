#include "ArchiveTool.h"
#include <QProcess>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>

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

bool ArchiveTool::extract(const QString& archivePath, const QString& destDir) {
    QString bin = sevenZipBinary();
    if (bin.isEmpty()) return false;
    QDir().mkpath(destDir);
    QProcess proc;
    proc.start(bin, {"x", archivePath, "-o" + destDir, "-y", "-bd"});
    if (!proc.waitForFinished(600000)) return false;
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

} // namespace solero
