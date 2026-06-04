#pragma once
#include <QString>
#include <QStringList>
#include <functional>

namespace solero {

class ArchiveTool {
public:
    // List archive entries as '/'-separated relative paths (files only).
    static QStringList listEntries(const QString& archivePath, bool* ok = nullptr);
    // Extract the whole archive into destDir (created if needed). Returns success.
    static bool extract(const QString& archivePath, const QString& destDir,
                        const std::function<void(int)>& onProgress = {});
    static bool isSolid(const QString& archivePath);
    // Extract only the given sub-paths (e.g. {"fomod"} or image dirs), responsive
    // (pumps the event loop, reports % via onProgress). Pass recursive=true to add -r.
    static bool extractPaths(const QString& archivePath, const QString& destDir,
                             const QStringList& paths, bool recursive = true,
                             const std::function<void(int)>& onProgress = {});
    static bool sevenZipAvailable();
private:
    static QString sevenZipBinary(); // "7z" or "7za"
};

} // namespace solero
