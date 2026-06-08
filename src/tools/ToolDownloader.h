#pragma once
#include "ToolCatalog.h"
#include <QString>
#include <functional>
namespace solero {
struct ToolDownloadResult { bool ok=false; QString exePath; QString error; QString iconPath; };
class ToolDownloader {
public:
    // Downloads + extracts the preset. onProgress(percent) for the download/extract.
    static ToolDownloadResult fetch(const ToolPreset& preset,
                                    const QString& downloadsDir,
                                    const QString& toolsRoot,
                                    const std::function<void(int)>& onProgress = {});
    static bool nexusApiKeyAvailable();
    static QString extractIcon(const QString& exePath, const QString& destDir);
    static QString nexusApiKey();
    // "" on failure; sets *error (when non-null) to a user-facing message, and
    // *fileName (when non-null) to the Nexus API file_name for the chosen file.
    static QString nexusDownloadUrl(const ToolPreset& p, QString* error = nullptr,
                                    QString* fileName = nullptr);
    static bool curlDownload(const QString& url, const QString& dest,
                             const QString& header, const std::function<void(int)>& onProgress,
                             QString* errorOut = nullptr);
private:
    static QString githubDownloadUrl(const ToolPreset& p, QString* fileName = nullptr);  // "" on failure
};
}
