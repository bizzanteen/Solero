#include "NxmHandler.h"
#include "NexusApi.h"
#include "tools/ToolDownloader.h"
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>

namespace solero {

static bool isNumericish(const QString& s) {
    if (s.isEmpty()) return false;
    for (QChar c : s) if (!c.isDigit()) return false;
    return true;
}

bool NxmHandler::isSupportedGame(const QString& game) {
    // Only the Skyrim games Solero manages. "skyrimspecialedition" is the primary
    // target; "skyrim" (LE) matches the LE id mapping in NexusApi. Non-Skyrim
    // domains (e.g. "oblivion", "fallout4") are rejected so they aren't downloaded
    // as junk. Matched case-insensitively.
    const QString g = game.toLower();
    return g == QStringLiteral("skyrimspecialedition")
        || g == QStringLiteral("skyrim");
}

NxmLink NxmHandler::parse(const QString& url) {
    NxmLink link;
    QUrl u(url, QUrl::StrictMode);
    link.game = u.host();
    // SkipEmptyParts so a trailing slash (or the leading slash) doesn't shift the
    // path segments: expect ["mods", "<modId>", "files", "<fileId>"].
    const QStringList parts = u.path().split('/', Qt::SkipEmptyParts);
    if (parts.size() == 4 && parts[0] == "mods" && parts[2] == "files") {
        link.modId = parts[1];
        link.fileId = parts[3];
    }
    QUrlQuery q(u);
    link.key = q.queryItemValue("key");
    link.expires = q.queryItemValue("expires");
    link.userId = q.queryItemValue("user_id");
    // A link is valid only for a Skyrim game domain Solero manages.
    link.valid = isSupportedGame(link.game)
              && isNumericish(link.modId) && isNumericish(link.fileId);
    return link;
}

// Runs curl with the Nexus apikey header. On curl failure sets *err and returns empty.
static QByteArray curlGet(const QString& url, QString* err = nullptr) {
    QProcess p;
    QStringList args; args << "-s" << "-H" << ("apikey: " + ToolDownloader::nexusApiKey()) << url;
    p.start("curl", args);
    p.waitForFinished(60000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (err) *err = "curl failed (exit " + QString::number(p.exitCode()) + ")";
        return {};
    }
    return p.readAllStandardOutput();
}

QString NxmHandler::resolveDownloadUrl(const NxmLink& link) {
    if (!link.valid) return {};
    QString url = "https://api.nexusmods.com/v1/games/" + link.game + "/mods/" + link.modId
                + "/files/" + link.fileId + "/download_link.json";
    // Only append key+expires when both are present (a dangling expires= breaks the
    // API call); free-account nxm links carry both, premium links carry neither.
    if (!link.key.isEmpty() && !link.expires.isEmpty()) {
        QUrlQuery q;
        q.addQueryItem("key", link.key);
        q.addQueryItem("expires", link.expires);
        url += "?" + q.query(QUrl::FullyEncoded);
    }
    QString err;
    QByteArray body = curlGet(url, &err);
    if (body.isEmpty()) return {};   // curl failed or empty body; caller warns generically
    auto arr = QJsonDocument::fromJson(body).array();
    if (arr.isEmpty()) return {};
    return arr[0].toObject()["URI"].toString();
}

QString NxmHandler::fileName(const NxmLink& link) {
    if (!link.valid) return {};
    QString url = "https://api.nexusmods.com/v1/games/" + link.game + "/mods/" + link.modId
                + "/files/" + link.fileId + ".json";
    QString err;
    QByteArray body = curlGet(url, &err);
    if (body.isEmpty()) return {};
    auto obj = QJsonDocument::fromJson(body).object();
    return obj["file_name"].toString();
}

QString NxmHandler::fileVersion(const NxmLink& link) {
    if (!link.valid) return {};
    return NexusApi::fileVersion(link.modId, link.fileId, link.game);
}

}
