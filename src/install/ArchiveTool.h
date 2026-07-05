#pragma once
#include <QString>
#include <QStringList>
#include <functional>

namespace solero {

class ArchiveTool {
public:
    // One archive entry with its stored CRC32 (IEEE, computed over the
    // uncompressed bytes - matches a zlib/std crc32 of the file on disk). crc is
    // 0 when the backend cannot report one (e.g. rar via unrar's bare listing).
    struct Entry { QString path; quint32 crc = 0; };

    // List archive entries as '/'-separated relative paths (files only).
    static QStringList listEntries(const QString& archivePath, bool* ok = nullptr);
    // Same listing but with each entry's stored CRC32 (7z `l -slt`). For rar the
    // crc is left 0 (the path list is still complete).
    static QList<Entry> listEntriesWithCrc(const QString& archivePath, bool* ok = nullptr);
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
    // Post-extract path-safety pass: walk destDir and reject any entry whose
    // resolved path escapes destDir (zip-slip / symlink-escape). Returns true
    // if every entry is contained.
    static bool validateExtraction(const QString& destDir);
    // Gate an extractor's success on validateExtraction; on a violation, wipe
    // destDir and return false so the extraction is treated as failed.
    static bool finalizeExtract(const QString& destDir, bool extractOk);
};

} // namespace solero
