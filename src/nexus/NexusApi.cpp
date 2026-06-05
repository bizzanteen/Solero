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

// post a raw JSON body (used for the v2 GraphQL endpoint). Adds the
// Content-Type header on top of curlRun's apikey header.
static QByteArray curlPostJson(const QString& url, const QByteArray& body, QString* err = nullptr) {
    QStringList args;
    args << "-H" << "Content-Type: application/json"
         << "-X" << "POST" << "-d" << QString::fromUtf8(body) << url;
    return curlRun(args, err);
}

// Map a Nexus game slug to its numeric v2 gameId. Defaults to SkyrimSE (1704).
static QString numericGameId(const QString& game) {
    if (game == "skyrim") return "110";       // Skyrim LE
    if (game == "fallout4") return "1151";
    return "1704";                            // skyrimspecialedition + default
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

// Browsing

static NexusApi::ModSummary summaryFromV1(const QJsonObject& o) {
    NexusApi::ModSummary s;
    int id = o["mod_id"].toInt();
    s.modId = id > 0 ? QString::number(id) : QString();
    s.name = o["name"].toString();
    s.summary = o["summary"].toString();
    s.author = o["author"].toString();
    if (s.author.isEmpty()) s.author = o["uploaded_by"].toString();
    s.pictureUrl = o["picture_url"].toString();
    s.endorsements = o["endorsement_count"].toInt();
    s.adult = o["contains_adult_content"].toBool();
    return s;
}

// Shared GET -> array -> ModSummary list for the v1 curated lists.
static QList<NexusApi::ModSummary> v1List(const QString& list, const QString& game) {
    QList<NexusApi::ModSummary> out;
    QByteArray body = curlGet(kBase + game + "/mods/" + list + ".json");
    if (body.isEmpty()) return out;
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) return out;
    for (const auto& v : doc.array()) {
        if (!v.isObject()) continue;
        auto s = summaryFromV1(v.toObject());
        if (!s.modId.isEmpty()) out.append(s);
    }
    return out;
}

QList<NexusApi::ModSummary> NexusApi::search(const QString& query, int count, const QString& game) {
    QList<ModSummary> out;
    QString text = query.trimmed();
    if (text.isEmpty()) return out;
    // Escape backslash then double-quote for embedding in the GraphQL string.
    text.replace("\\", "\\\\").replace("\"", "\\\"");
    const QString gameId = numericGameId(game);
    const int n = count > 0 ? count : 25;
    const QString q = QString(
        "query{mods(filter:{name:{value:\"%1\",op:WILDCARD},"
        "gameId:{value:\"%2\",op:EQUALS}},count:%3){nodes{"
        "modId name summary uploader{name} pictureUrl thumbnailUrl "
        "endorsements adultContent}}}").arg(text, gameId).arg(n);
    QJsonObject reqObj; reqObj["query"] = q;
    QByteArray reqBody = QJsonDocument(reqObj).toJson(QJsonDocument::Compact);
    QByteArray body = curlPostJson("https://api.nexusmods.com/v2/graphql", reqBody);
    if (body.isEmpty()) return out;
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return out;
    auto nodes = doc.object()["data"].toObject()["mods"].toObject()["nodes"].toArray();
    for (const auto& v : nodes) {
        auto o = v.toObject();
        ModSummary s;
        int id = o["modId"].toInt();
        s.modId = id > 0 ? QString::number(id) : QString();
        s.name = o["name"].toString();
        s.summary = o["summary"].toString();
        s.author = o["uploader"].toObject()["name"].toString();
        s.pictureUrl = o["pictureUrl"].toString();
        if (s.pictureUrl.isEmpty()) s.pictureUrl = o["thumbnailUrl"].toString();
        s.endorsements = o["endorsements"].toInt();
        s.adult = o["adultContent"].toBool();
        if (!s.modId.isEmpty()) out.append(s);
    }
    return out;
}

QList<NexusApi::ModSummary> NexusApi::trending(const QString& game)     { return v1List("trending", game); }
QList<NexusApi::ModSummary> NexusApi::latestAdded(const QString& game)  { return v1List("latest_added", game); }
QList<NexusApi::ModSummary> NexusApi::latestUpdated(const QString& game){ return v1List("latest_updated", game); }

NexusApi::ModDetails NexusApi::modDetails(const QString& modId, const QString& game) {
    ModDetails r;
    if (modId.isEmpty()) return r;
    QByteArray body = curlGet(kBase + game + "/mods/" + modId + ".json");
    if (body.isEmpty()) return r;
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return r;
    auto o = doc.object();
    int id = o["mod_id"].toInt();
    r.modId = id > 0 ? QString::number(id) : modId;
    r.name = o["name"].toString();
    r.summary = o["summary"].toString();
    r.description = o["description"].toString();   // raw BBCode
    r.pictureUrl = o["picture_url"].toString();
    r.author = o["author"].toString();
    if (r.author.isEmpty()) r.author = o["uploaded_by"].toString();
    r.version = o["version"].toString();
    r.endorsements = o["endorsement_count"].toInt();
    r.adult = o["contains_adult_content"].toBool();
    r.endorseStatus = o["endorsement"].toObject()["endorse_status"].toString();
    r.ok = !r.name.isEmpty();
    return r;
}

QList<NexusApi::NexusFile> NexusApi::files(const QString& modId, const QString& game) {
    QList<NexusFile> out;
    if (modId.isEmpty()) return out;
    QByteArray body = curlGet(kBase + game + "/mods/" + modId + "/files.json");
    if (body.isEmpty()) return out;
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return out;
    for (const auto& v : doc.object()["files"].toArray()) {
        auto o = v.toObject();
        NexusFile f;
        int id = o["file_id"].toInt();
        f.fileId = id > 0 ? QString::number(id) : QString();
        f.name = o["file_name"].toString();
        f.version = o["version"].toString();
        f.category = o["category_name"].toString();
        f.description = o["description"].toString();
        f.sizeKb = static_cast<qint64>(o["size_kb"].toDouble());
        if (f.sizeKb == 0) f.sizeKb = static_cast<qint64>(o["size"].toDouble());
        if (!f.fileId.isEmpty()) out.append(f);
    }
    return out;
}

QString NexusApi::downloadUrl(const QString& modId, const QString& fileId, const QString& game) {
    if (modId.isEmpty() || fileId.isEmpty()) return {};
    QByteArray body = curlGet(kBase + game + "/mods/" + modId + "/files/" + fileId + "/download_link.json");
    if (body.isEmpty()) return {};
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) return {};   // non-premium returns an error object
    auto arr = doc.array();
    if (arr.isEmpty()) return {};
    return arr[0].toObject()["URI"].toString();
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
