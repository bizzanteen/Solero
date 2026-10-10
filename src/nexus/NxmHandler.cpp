#include "NxmHandler.h"
#include "NexusApi.h"
#include "tools/ToolDownloader.h"
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

namespace solero {

static bool isNumericish(const QString& s) {
    if (s.isEmpty()) return false;
    for (QChar c : s) if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
    return true;
}

bool NxmHandler::isSupportedGame(const QString& game) {
    // Domains are deliberately allow-listed: accepting arbitrary nxm:// links can
    // make Solero download unrelated or unsupported game files. Oblivion Remastered
    // uses its own Nexus domain (distinct from classic Oblivion).
    const QString g = game.toLower();
    return g == QStringLiteral("skyrimspecialedition")
        || g == QStringLiteral("skyrim")
        || g == QStringLiteral("oblivionremastered");
}

NxmLink NxmHandler::parse(const QString& url) {
    NxmLink link;
    QUrl u(url, QUrl::StrictMode);
    if (!u.isValid() || u.scheme().compare("nxm", Qt::CaseInsensitive) != 0
        || !u.userInfo().isEmpty() || u.port(-1) != -1 || !u.fragment().isEmpty())
        return link;
    link.game = u.host().toLower();
    static const QRegularExpression pathPattern(
        QStringLiteral("^/mods/([0-9]+)/files/([0-9]+)/?$"));
    const auto pathMatch = pathPattern.match(u.path(QUrl::FullyEncoded));
    if (pathMatch.hasMatch()) {
        link.modId = pathMatch.captured(1);
        link.fileId = pathMatch.captured(2);
    }
    QUrlQuery q(u);
    const auto keys = q.allQueryItemValues("key", QUrl::FullyDecoded);
    const auto expiries = q.allQueryItemValues("expires", QUrl::FullyDecoded);
    const auto users = q.allQueryItemValues("user_id", QUrl::FullyDecoded);
    if (keys.size() > 1 || expiries.size() > 1 || users.size() > 1
        || (!keys.isEmpty() && keys.first().isEmpty())
        || (!expiries.isEmpty() && expiries.first().isEmpty())
        || keys.isEmpty() != expiries.isEmpty())
        return link;
    link.key = q.queryItemValue("key");
    link.expires = q.queryItemValue("expires");
    link.userId = q.queryItemValue("user_id");
    // A link is valid only for a game domain Solero explicitly supports.
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
    return NexusApi::downloadUrl(link.modId, link.fileId, link.game, link.key, link.expires);
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
