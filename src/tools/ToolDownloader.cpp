#include "ToolDownloader.h"
#include "install/ArchiveTool.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QUrl>

namespace solero {

QString ToolDownloader::nexusApiKey() {
    QFile f(QDir::homePath() + "/.nexus_api_key");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}
bool ToolDownloader::nexusApiKeyAvailable() { return !nexusApiKey().isEmpty(); }

static QByteArray curlJson(const QString& url, const QString& header) {
    QProcess p;
    QStringList args; args << "-s";
    if (!header.isEmpty()) { args << "-H" << header; }
    args << url;
    p.start("curl", args);
    p.waitForFinished(60000);
    return p.readAllStandardOutput();
}

QString ToolDownloader::nexusDownloadUrl(const ToolPreset& pr) {
    QString key = nexusApiKey();
    if (key.isEmpty()) return {};
    QString base = "https://api.nexusmods.com/v1/games/" + pr.nexusGame + "/mods/" + pr.nexusModId;
    auto files = QJsonDocument::fromJson(curlJson(base + "/files.json", "apikey: " + key)).object();
    int fileId = -1;
    for (const auto& v : files["files"].toArray()) {
        auto o = v.toObject();
        if (o["is_primary"].toBool() || o["category_name"].toString() == "MAIN") { fileId = o["file_id"].toInt(); }
    }
    if (fileId < 0) return {};
    auto link = QJsonDocument::fromJson(
        curlJson(base + "/files/" + QString::number(fileId) + "/download_link.json", "apikey: " + key)).array();
    if (link.isEmpty()) return {};
    return link[0].toObject()["URI"].toString();
}

QString ToolDownloader::githubDownloadUrl(const ToolPreset& pr) {
    QString api = "https://api.github.com/repos/" + pr.githubOwner + "/" + pr.githubRepo + "/releases/latest";
    auto rel = QJsonDocument::fromJson(curlJson(api, "")).object();
    QString match = pr.assetMatch.toLower();
    for (const auto& v : rel["assets"].toArray()) {
        auto o = v.toObject();
        if (o["name"].toString().toLower().contains(match))
            return o["browser_download_url"].toString();
    }
    // fall back to first asset
    auto assets = rel["assets"].toArray();
    return assets.isEmpty() ? QString() : assets[0].toObject()["browser_download_url"].toString();
}

bool ToolDownloader::curlDownload(const QString& url, const QString& dest,
                                  const QString& header, const std::function<void(int)>& onProgress) {
    QDir().mkpath(QFileInfo(dest).path());
    // URL-encode spaces (Nexus CDN URLs contain them).
    QString safe = QString::fromLatin1(QUrl(url).toEncoded());
    QProcess p;
    QStringList args; args << "-L" << "--fail";
    if (!header.isEmpty()) args << "-H" << header;
    args << "-o" << dest << safe;
    p.start("curl", args);
    if (!p.waitForStarted(15000)) return false;
    while (p.state() != QProcess::NotRunning) {
        p.waitForFinished(200);
        if (onProgress) onProgress(-1); // indeterminate (curl progress parsing omitted)
    }
    return p.exitCode() == 0 && QFileInfo(dest).size() > 0;
}

ToolDownloadResult ToolDownloader::fetch(const ToolPreset& preset, const QString& downloadsDir,
                                         const QString& toolsRoot,
                                         const std::function<void(int)>& onProgress) {
    ToolDownloadResult r;
    QString url = preset.source == ToolSource::Nexus ? nexusDownloadUrl(preset)
                                                     : githubDownloadUrl(preset);
    if (url.isEmpty()) { r.error = "Could not resolve a download URL (Nexus key missing or release not found)."; return r; }

    QDir().mkpath(downloadsDir);
    QString ext = url.contains(".7z") ? ".7z" : (url.contains(".rar") ? ".rar" : ".zip");
    QString archive = downloadsDir + "/" + preset.id + ext;
    QString header = preset.source == ToolSource::Nexus ? ("apikey: " + nexusApiKey()) : QString();
    if (!curlDownload(url, archive, header, onProgress)) { r.error = "Download failed."; return r; }

    QString dest = toolsRoot + "/" + preset.id;
    QDir(dest).removeRecursively();
    if (!ArchiveTool::extract(archive, dest, onProgress)) { r.error = "Extraction failed."; return r; }

    // Flatten a single wrapper dir if present.
    QDir d(dest);
    auto subdirs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (subdirs.size() == 1 && d.entryList(QDir::Files).isEmpty()) {
        QString inner = dest + "/" + subdirs.first();
        for (const QString& e : QDir(inner).entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
            QFile::rename(inner + "/" + e, dest + "/" + e);
        QDir(inner).removeRecursively();
    }

    // Resolve exe path case-insensitively.
    if (!preset.exeRelPath.isEmpty()) {
        r.exePath = dest + "/" + preset.exeRelPath;
        if (!QFile::exists(r.exePath)) {
            // shallow search
            QDir dd(dest);
            for (const QString& e : dd.entryList(QDir::Files))
                if (e.compare(preset.exeRelPath, Qt::CaseInsensitive) == 0) { r.exePath = dest + "/" + e; break; }
        }
    }
    r.ok = true;
    return r;
}
}
