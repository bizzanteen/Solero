#pragma once
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace solero {

// dotnet/sdk holds one directory per installed SDK, named for its version. The
// comparison has to be numeric: "8.0.423" sorts above "10.0.302" as a string.

// Major version of an SDK version string, or 0 if it isn't one.
inline int sdkMajor(const QString& version) {
    const QString head = version.section('.', 0, 0);
    bool ok = false;
    const int major = head.toInt(&ok);
    return (ok && major > 0) ? major : 0;
}

// Highest major among SDK folder names (non-version entries like
// NuGetFallbackFolder are ignored). 0 when none are installed.
inline int highestSdkMajor(const QStringList& versionDirs) {
    int best = 0;
    for (const QString& d : versionDirs) best = qMax(best, sdkMajor(d));
    return best;
}

// Does this text carry the MSBuild error a too-old SDK produces? Synthesis shows
// it as "NET SDK Out of Date" in its UI and logs NETSDK1045 ("The current .NET SDK
// does not support targeting .NET <x>") for every attempt.
inline bool showsSdkTooOld(const QString& text) {
    return text.contains(QStringLiteral("NETSDK1045"));
}

// Same check against a tool's own logs. Synthesis reports the build failure in its
// UI and its Serilog file, not on stdout. Only logs written at or after `since`
// count, so an old failure can't re-trigger the prompt. (Deliberately a small local
// read rather than a shared log-scanning layer - there are only two of these.)
inline bool sdkTooOldInToolLogs(const QString& installDir, const QDateTime& since) {
    QDir logs(installDir + "/logs");
    if (!logs.exists()) return false;
    for (const QFileInfo& fi : logs.entryInfoList({"*.log"}, QDir::Files)) {
        if (fi.lastModified() < since) continue;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        constexpr qint64 kTail = 256 * 1024;
        if (f.size() > kTail) f.seek(f.size() - kTail);
        if (showsSdkTooOld(QString::fromUtf8(f.readAll()))) return true;
    }
    return false;
}

} // namespace solero
