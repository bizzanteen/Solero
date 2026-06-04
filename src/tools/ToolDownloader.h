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
private:
    static QString nexusApiKey();
    static QString nexusDownloadUrl(const ToolPreset& p);   // "" on failure
    static QString githubDownloadUrl(const ToolPreset& p);  // "" on failure
    static bool curlDownload(const QString& url, const QString& dest,
                             const QString& header, const std::function<void(int)>& onProgress);
};
}
