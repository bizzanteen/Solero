#include "ToolDownloader.h"
#include "install/ArchiveTool.h"
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
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

// Runs curl and returns the body. On failure (non-zero curl exit) sets *err and
// returns empty. The body is not validated as JSON here; callers parse it.
static QByteArray curlJson(const QString& url, const QString& header, QString* err = nullptr) {
    QProcess p;
    QStringList args; args << "-s";
    if (!header.isEmpty()) { args << "-H" << header; }
    args << url;
    p.start("curl", args);
    p.waitForFinished(60000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (err) *err = "Network request failed (curl exit " + QString::number(p.exitCode())
                      + "): " + QString::fromUtf8(p.readAllStandardError()).trimmed();
        return {};
    }
    return p.readAllStandardOutput();
}

QString ToolDownloader::nexusDownloadUrl(const ToolPreset& pr, QString* error, QString* fileName) {
    QString key = nexusApiKey();
    if (key.isEmpty()) { if (error) *error = "No Nexus API key configured (~/.nexus_api_key)."; return {}; }
    QString base = "https://api.nexusmods.com/v1/games/" + pr.nexusGame + "/mods/" + pr.nexusModId;

    QString curlErr;
    QByteArray filesBody = curlJson(base + "/files.json", "apikey: " + key, &curlErr);
    if (filesBody.isEmpty()) { if (error) *error = curlErr; return {}; }
    QJsonParseError pe{};
    auto filesDoc = QJsonDocument::fromJson(filesBody, &pe);
    if (pe.error != QJsonParseError::NoError || !filesDoc.isObject()) {
        if (error) *error = "Nexus API returned an unexpected response (not JSON): "
                          + QString::fromUtf8(filesBody.left(200)).trimmed();
        return {};
    }
    auto files = filesDoc.object();
    int fileId = -1; int bestMainId = -1; QString chosenName;
    QString bestMainName;
    for (const auto& v : files["files"].toArray()) {
        auto o = v.toObject();
        int fid = o["file_id"].toInt();
        if (o["is_primary"].toBool()) { fileId = fid; chosenName = o["file_name"].toString(); break; }
        if (o["category_name"].toString() == "MAIN" && fid > bestMainId) {
            bestMainId = fid; bestMainName = o["file_name"].toString();
        }
    }
    if (fileId < 0) { fileId = bestMainId; chosenName = bestMainName; }
    if (fileId < 0) { if (error) *error = "No primary/main file found for this mod on Nexus."; return {}; }
    if (fileName) *fileName = chosenName;

    QByteArray linkBody = curlJson(
        base + "/files/" + QString::number(fileId) + "/download_link.json", "apikey: " + key, &curlErr);
    auto linkDoc = QJsonDocument::fromJson(linkBody);
    auto link = linkDoc.array();
    if (link.isEmpty()) {
        // Free accounts get a 403/empty here: direct download needs Premium.
        if (error) *error = "This file needs Nexus Premium for direct download, or use the "
                            "mod page's 'Mod Manager Download' (nxm) button.";
        return {};
    }
    return link[0].toObject()["URI"].toString();
}

QString ToolDownloader::githubDownloadUrl(const ToolPreset& pr, QString* fileName) {
    QString api = "https://api.github.com/repos/" + pr.githubOwner + "/" + pr.githubRepo + "/releases/latest";
    auto rel = QJsonDocument::fromJson(curlJson(api, "")).object();
    QString match = pr.assetMatch.toLower();
    for (const auto& v : rel["assets"].toArray()) {
        auto o = v.toObject();
        if (o["name"].toString().toLower().contains(match)) {
            if (fileName) *fileName = o["name"].toString();
            return o["browser_download_url"].toString();
        }
    }
    // fall back to first asset
    auto assets = rel["assets"].toArray();
    if (assets.isEmpty()) return {};
    auto first = assets[0].toObject();
    if (fileName) *fileName = first["name"].toString();
    return first["browser_download_url"].toString();
}

bool ToolDownloader::curlDownload(const QString& url, const QString& dest,
                                  const QString& header, const std::function<void(int)>& onProgress,
                                  QString* errorOut) {
    QDir().mkpath(QFileInfo(dest).path());
    // URL-encode spaces (Nexus CDN URLs contain them).
    QString safe = QString::fromLatin1(QUrl(url).toEncoded());
    QProcess p;
    // GitHub's release CDN intermittently returns transient 5xx (e.g. 504 Gateway
    // Timeout) and connections occasionally stall; let curl handle retries itself.
    // --retry-all-errors makes it retry HTTP 5xx too (plain --retry only covers
    // connection-level failures), and -sS keeps it quiet while still printing the
    // real error to stderr so we can surface it.
    QStringList args; args << "-L" << "--fail"
                          << "-sS" << "--retry" << "4" << "--retry-all-errors"
                          << "--retry-delay" << "2" << "--connect-timeout" << "30"
                          << "--retry-max-time" << "300";
    if (!header.isEmpty()) args << "-H" << header;
    args << "-o" << dest << safe;
    p.start("curl", args);
    if (!p.waitForStarted(15000)) {
        QFile::remove(dest);
        if (errorOut) *errorOut = "curl failed to start";
        return false;
    }
    while (p.state() != QProcess::NotRunning) {
        p.waitForFinished(200);
        if (onProgress) onProgress(-1); // indeterminate (curl progress parsing omitted)
    }
    const bool ok = p.exitCode() == 0 && QFileInfo(dest).size() > 0;
    if (!ok) {
        QFile::remove(dest);   // never leave a partial/empty archive behind
        if (errorOut) {
            const QString stderrText = QString::fromUtf8(p.readAllStandardError()).trimmed();
            QString lastLine;
            for (const QString& line : stderrText.split('\n', Qt::SkipEmptyParts))
                if (!line.trimmed().isEmpty()) lastLine = line.trimmed();
            *errorOut = "curl exit " + QString::number(p.exitCode())
                      + (lastLine.isEmpty() ? "" : ": " + lastLine);
        }
    }
    return ok;
}

ToolDownloadResult ToolDownloader::fetch(const ToolPreset& preset, const QString& downloadsDir,
                                         const QString& toolsRoot,
                                         const std::function<void(int)>& onProgress) {
    ToolDownloadResult r;
    QString resolveErr;
    QString remoteName;  // Nexus/GitHub file_name, used to derive the real extension.
    QString url = preset.source == ToolSource::Nexus
                      ? nexusDownloadUrl(preset, &resolveErr, &remoteName)
                      : githubDownloadUrl(preset, &remoteName);
    if (url.isEmpty()) {
        r.error = !resolveErr.isEmpty() ? resolveErr
                                        : "Could not resolve a download URL (release not found).";
        return r;
    }

    QDir().mkpath(downloadsDir);
    // Derive the archive extension from the real filename (API file_name, else the
    // URL's filename) so ArchiveTool's .rar/.7z routing works; guessing by url.contains
    // misfires when the extension only appears in a query string.
    auto extOf = [](const QString& name) -> QString {
        const QString suffix = QFileInfo(name).suffix().toLower();
        if (suffix == "7z" || suffix == "rar" || suffix == "zip" || suffix == "tar" || suffix == "gz")
            return "." + suffix;
        return {};
    };
    QString ext = extOf(remoteName);
    if (ext.isEmpty()) ext = extOf(QUrl(url).fileName());
    if (ext.isEmpty()) ext = ".zip";  // last-resort default
    QString archive = downloadsDir + "/" + preset.id + ext;
    QString header = preset.source == ToolSource::Nexus ? ("apikey: " + nexusApiKey()) : QString();
    QString dlErr;
    if (!curlDownload(url, archive, header, onProgress, &dlErr)) {
        QFile::remove(archive);   // ensure no partial/empty archive lingers for re-extract
        r.error = dlErr.isEmpty() ? "Download failed." : "Download failed - " + dlErr;
        return r;
    }

    QString dest = toolsRoot + "/" + preset.id;
    QDir(dest).removeRecursively();
    if (!ArchiveTool::extract(archive, dest, onProgress)) {
        QFile::remove(archive);   // a corrupt/partial archive that won't extract - don't keep it
        r.error = "Extraction failed.";
        return r;
    }
    // Tool archive is dead weight once extracted into the tools dir; unlike mod
    // archives there's no reinstall/sourceArchive use for it, so reclaim the space.
    QFile::remove(archive);

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
        if (!QFile::exists(r.exePath)) {
            // recursive search (archives may nest one or more levels deeper)
            QDirIterator it(dest, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QString f = it.next();
                if (QFileInfo(f).fileName().compare(preset.exeRelPath, Qt::CaseInsensitive) == 0) {
                    r.exePath = f; break;
                }
            }
        }
        // Native (non-Proton) binaries download without an exec bit; set it.
        if (!preset.proton && !r.exePath.isEmpty() && QFile::exists(r.exePath)) {
            QFile bf(r.exePath);
            bf.setPermissions(bf.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeUser);
        }
        // Extract a real icon from Windows .exe tools.
        if (preset.proton && !r.exePath.isEmpty() && QFile::exists(r.exePath)) {
            r.iconPath = extractIcon(r.exePath, dest);
        }
    }
    r.ok = true;
    return r;
}

QString ToolDownloader::extractIcon(const QString& exePath, const QString& destDir) {
    if (exePath.isEmpty() || !QFile::exists(exePath)) return {};
    QString ico = destDir + "/_icon.ico";
    QProcess wr;
    wr.start("wrestool", {"-x", "-t", "14", "-o", ico, exePath});
    wr.waitForFinished(20000);
    if (!QFile::exists(ico) || QFileInfo(ico).size() == 0) return {};
    // icotool extracts one png per icon size; ask for all then pick the largest.
    QProcess ic;
    ic.start("icotool", {"-x", "-o", destDir, ico});
    ic.waitForFinished(20000);
    // icotool names files like _icon_1_32x32x32.png in destDir; find the biggest png.
    QString best; qint64 bestArea = -1;
    QDir d(destDir);
    for (const QString& f : d.entryList({"_icon*.png"}, QDir::Files)) {
        QString full = destDir + "/" + f;
        QImage img(full);
        qint64 area = (qint64)img.width() * img.height();
        if (area > bestArea) { bestArea = area; best = full; }
    }
    if (best.isEmpty()) {
        // fallback: imagemagick convert the .ico directly
        QString png = destDir + "/icon.png";
        QProcess cv; cv.start("convert", {ico + "[0]", png}); cv.waitForFinished(20000);
        if (QFile::exists(png) && QFileInfo(png).size() > 0) return png;
        return {};
    }
    // normalize to icon.png
    QString png = destDir + "/icon.png";
    QFile::remove(png);
    QFile::copy(best, png);
    return png;
}
}
