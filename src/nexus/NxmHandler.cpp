#include "NxmHandler.h"
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

NxmLink NxmHandler::parse(const QString& url) {
    NxmLink link;
    QUrl u(url, QUrl::StrictMode);
    link.game = u.host();
    const QStringList parts = u.path().split('/');
    // expect ["", "mods", "<modId>", "files", "<fileId>"]
    if (parts.size() == 5 && parts[1] == "mods" && parts[3] == "files") {
        link.modId = parts[2];
        link.fileId = parts[4];
    }
    QUrlQuery q(u);
    link.key = q.queryItemValue("key");
    link.expires = q.queryItemValue("expires");
    link.userId = q.queryItemValue("user_id");
    link.valid = !link.game.isEmpty() && isNumericish(link.modId) && isNumericish(link.fileId);
    return link;
}

static QByteArray curlGet(const QString& url) {
    QProcess p;
    QStringList args; args << "-s" << "-H" << ("apikey: " + ToolDownloader::nexusApiKey()) << url;
    p.start("curl", args);
    p.waitForFinished(60000);
    return p.readAllStandardOutput();
}

QString NxmHandler::resolveDownloadUrl(const NxmLink& link) {
    if (!link.valid) return {};
    QString url = "https://api.nexusmods.com/v1/games/" + link.game + "/mods/" + link.modId
                + "/files/" + link.fileId + "/download_link.json";
    if (!link.key.isEmpty()) {
        QUrlQuery q;
        q.addQueryItem("key", link.key);
        q.addQueryItem("expires", link.expires);
        url += "?" + q.query(QUrl::FullyEncoded);
    }
    auto arr = QJsonDocument::fromJson(curlGet(url)).array();
    if (arr.isEmpty()) return {};
    return arr[0].toObject()["URI"].toString();
}

QString NxmHandler::fileName(const NxmLink& link) {
    if (!link.valid) return {};
    QString url = "https://api.nexusmods.com/v1/games/" + link.game + "/mods/" + link.modId
                + "/files/" + link.fileId + ".json";
    auto obj = QJsonDocument::fromJson(curlGet(url)).object();
    return obj["file_name"].toString();
}

}
