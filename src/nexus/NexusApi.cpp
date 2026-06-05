#include "NexusApi.h"
#include "tools/ToolDownloader.h"
#include "core/FileUtil.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDir>
#include <QFile>

namespace solero {

static const QString kBase = "https://api.nexusmods.com/v1/games/";

// Runs `curl -s [args...]` with the apikey header. On curl failure sets *err and
// returns empty. The body is not validated as JSON; callers parse it.
static QByteArray curlRun(const QStringList& extraArgs, QString* err = nullptr) {
    const QString key = ToolDownloader::nexusApiKey();
    QProcess p;
    QStringList args; args << "-s" << "-H" << ("apikey: " + key) << extraArgs;
    p.start("curl", args);
    p.waitForFinished(60000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (err) *err = "Network request failed (curl exit " + QString::number(p.exitCode()) + ")";
        return {};
    }
    return p.readAllStandardOutput();
}

static QByteArray curlGet(const QString& url, QString* err = nullptr) {
    return curlRun(QStringList() << url, err);
}

bool NexusApi::keyAvailable() { return !ToolDownloader::nexusApiKey().isEmpty(); }

NexusApi::ModInfo NexusApi::modInfo(const QString& modId, const QString& game) {
    ModInfo r;
    if (modId.isEmpty()) return r;
    QString url = kBase + game + "/mods/" + modId + ".json";
    QByteArray body = curlGet(url);
    if (body.isEmpty()) return r;
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return r;
    auto o = doc.object();
    r.version = o["version"].toString();
    r.name = o["name"].toString();
    r.endorseStatus = o["endorsement"].toObject()["endorse_status"].toString();
    r.ok = !r.name.isEmpty() || !r.version.isEmpty();
    return r;
}

QString NexusApi::latestVersion(const QString& modId, const QString& game) {
    return modInfo(modId, game).version;
}

NexusApi::EndorseResult NexusApi::endorse(const QString& modId, const QString& version, bool abstain,
                                          const QString& game) {
    EndorseResult r;
    if (modId.isEmpty()) { r.message = "No Nexus mod id."; return r; }
    QString url = kBase + game + "/mods/" + modId + "/" + (abstain ? "abstain" : "endorse") + ".json";
    // Send version both as a query parameter and a form body for robustness.
    if (!version.isEmpty()) {
        QUrlQuery q; q.addQueryItem("version", version);
        url += "?" + q.query(QUrl::FullyEncoded);
    }
    QStringList args;
    args << "-X" << "POST";
    if (!version.isEmpty()) args << "-d" << ("version=" + version);
    args << url;
    QString err;
    QByteArray body = curlRun(args, &err);
    if (body.isEmpty()) { r.message = err.isEmpty() ? "No response from Nexus." : err; return r; }
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) { r.message = QString::fromUtf8(body.left(200)).trimmed(); return r; }
    auto o = doc.object();
    const QString status = o["status"].toString();
    const QString message = o["message"].toString();
    r.message = !message.isEmpty() ? message : status;
    // Success if the status reflects the requested state, or there's no error-like
    // message. Nexus errors come back as uppercase codes (NOT_DOWNLOADED_MOD,
    // TOO_SOON_AFTER_DOWNLOAD, ...) in the message field.
    const bool statusOk = (status.compare("Endorsed", Qt::CaseInsensitive) == 0
                           || status.compare("Abstained", Qt::CaseInsensitive) == 0);
    const bool errorLike = message.contains('_') && message == message.toUpper() && !message.isEmpty();
    r.ok = statusOk || (!errorLike && !message.isEmpty() && message != "Error");
    return r;
}

NexusApi::Md5Match NexusApi::md5Search(const QString& md5, const QString& game) {
    Md5Match r;
    if (md5.isEmpty()) return r;
    QString url = kBase + game + "/mods/md5_search/" + md5 + ".json";
    QByteArray body = curlGet(url);
    if (body.isEmpty()) return r;
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) return r;
    auto arr = doc.array();
    if (arr.isEmpty()) return r;
    auto first = arr[0].toObject();
    auto modObj = first["mod"].toObject();
    auto fd = first["file_details"].toObject();
    int modIdNum = modObj["mod_id"].toInt();
    int fileIdNum = fd["file_id"].toInt();
    r.modId = modIdNum > 0 ? QString::number(modIdNum) : QString();
    r.fileId = fileIdNum > 0 ? QString::number(fileIdNum) : QString();
    r.version = fd["version"].toString();
    if (r.version.isEmpty()) r.version = modObj["version"].toString();
    r.modName = modObj["name"].toString();
    r.ok = !r.modId.isEmpty();
    return r;
}

QString NexusApi::fileVersion(const QString& modId, const QString& fileId, const QString& game) {
    if (modId.isEmpty() || fileId.isEmpty()) return {};
    QString url = kBase + game + "/mods/" + modId + "/files/" + fileId + ".json";
    QByteArray body = curlGet(url);
    if (body.isEmpty()) return {};
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return {};
    return doc.object()["version"].toString();
}

QString NexusApi::apiKeyPath() {
    return QDir::homePath() + "/.nexus_api_key";
}

NexusApi::UserInfo NexusApi::validateUser(const QString& key) {
    UserInfo r;
    const QString useKey = key.isEmpty() ? ToolDownloader::nexusApiKey() : key;
    if (useKey.isEmpty()) return r;
    // Explicit curl call (not curlRun, which uses the stored key) with a short
    // timeout so the Settings dialog can't hang on open.
    QProcess p;
    QStringList args;
    args << "-s" << "--max-time" << "10"
         << "-H" << ("apikey: " + useKey)
         << "https://api.nexusmods.com/v1/users/validate.json";
    p.start("curl", args);
    p.waitForFinished(15000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) return r;
    QByteArray body = p.readAllStandardOutput();
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return r;
    auto o = doc.object();
    r.name = o["name"].toString();
    r.premium = o["is_premium"].toBool();
    r.ok = !r.name.isEmpty();
    return r;
}

bool NexusApi::setApiKey(const QString& key) {
    return atomicWrite(apiKeyPath(), key.trimmed().toUtf8());
}

void NexusApi::clearApiKey() {
    QFile::remove(apiKeyPath());
}

} // namespace solero
