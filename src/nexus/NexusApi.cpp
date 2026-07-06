#include "NexusApi.h"
#include "MirrorPick.h"
#include "core/Log.h"
#include "core/AppConfig.h"
#include "tools/ToolDownloader.h"
#include "tools/CurlError.h"
#include "core/FileUtil.h"
#include <QProcess>
#include "core/HostProcess.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDir>
#include <QFile>

namespace solero {

static const QString kBase = "https://api.nexusmods.com/v1/games/";

QString NexusApi::modPageUrl(const QString& modId, const QString& game) {
    return "https://www.nexusmods.com/" + game + "/mods/" + modId;
}

// Splits the HTTP status code that `-w %{stderr}%{http_code}` appends to stderr
// off from any curl diagnostic text. Returns the code (0 if none) and trims the
// trailing digits from *stderrText so the remainder is a clean error hint.
static int takeHttpCode(QString& stderrText) {
    int i = stderrText.size();
    while (i > 0 && stderrText.at(i - 1).isDigit()) --i;
    const QString digits = stderrText.mid(i);
    if (digits.isEmpty()) return 0;
    stderrText.truncate(i);
    return digits.right(3).toInt(); // http_code is always 3 digits
}

// Maps a mapped HTTP status to a user-facing message, or "" for a status we let
// callers handle as an ordinary (possibly empty) body..
static QString httpStatusMessage(int code) {
    if (code == 401) return "Nexus rejected your API key (401) - it may be expired or invalid. "
                            "Re-enter it in Settings.";
    if (code == 403) return "Nexus refused the request (403) - your account may lack access, "
                            "or the download requires Nexus Premium.";
    if (code == 429) return "Nexus is rate-limiting requests (429). Wait a minute and try again.";
    if (code >= 500) return "Nexus is having server problems (" + QString::number(code) + "). "
                            "Try again later.";
    return {};
}

// Runs `curl [args...]` with the apikey header supplied via a stdin config file
// (never argv). On curl/HTTP failure sets *err and returns empty. The
// body is not validated as JSON; callers parse it.
static QByteArray curlRun(const QStringList& extraArgs, QString* err = nullptr) {
    const QString key = ToolDownloader::nexusApiKey();
    QProcess p;
    // -K - reads further options from stdin; %{stderr} routes the status code to
    // stderr so it never contaminates the JSON body on stdout.
    QStringList args; args << "-sS" << "-w" << "%{stderr}%{http_code}" << "-K" << "-" << extraArgs;
    const auto hc = solero::hostCommand("curl", args, {}, solero::runningInFlatpak());
    p.start(hc.program, hc.args);
    // Feed the apikey header through the config channel, out of argv/ps/proc.
    p.write(QByteArray("header = \"apikey: ") + key.toUtf8() + "\"\n");
    p.closeWriteChannel();
    p.waitForFinished(60000);
    QString stderrText = QString::fromUtf8(p.readAllStandardError());
    const int httpCode = takeHttpCode(stderrText);
    stderrText = stderrText.trimmed();
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        // NB: never log `args` - the config channel carries the apikey header.
        qCWarning(lcNexus) << "curl request failed, exit code" << p.exitCode();
        if (err) {
            const QString hint = curlStderrHint(stderrText);
            *err = "No response from Nexus - check your internet connection."
                 + (hint.isEmpty() ? QString() : " (" + hint + ")");
        }
        return {};
    }
    const QString statusMsg = httpStatusMessage(httpCode);
    if (!statusMsg.isEmpty()) {
        qCWarning(lcNexus) << "Nexus HTTP status" << httpCode;
        if (err) *err = statusMsg;
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
    qCInfo(lcNexus) << "modInfo" << game << "mod" << modId;
    QString url = kBase + game + "/mods/" + modId + ".json";
    QByteArray body = curlGet(url);
    if (body.isEmpty()) { qCWarning(lcNexus) << "modInfo: empty response for mod" << modId; return r; }
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
    qCInfo(lcNexus) << (abstain ? "abstain" : "endorse") << game << "mod" << modId << "version" << version;
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
    if (body.isEmpty()) { qCWarning(lcNexus) << "endorse: empty response for mod" << modId; r.message = err.isEmpty() ? "No response from Nexus." : err; return r; }
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) { r.message = QString::fromUtf8(body.left(200)).trimmed(); return r; }
    auto o = doc.object();
    const QString status = o["status"].toString();
    const QString message = o["message"].toString();
    r.message = !message.isEmpty() ? message : status;
    // Nexus error codes are terse and user-hostile; translate the known ones.
    if (message == "NOT_DOWNLOADED_MOD")
        r.message = "Nexus only lets you endorse mods you've downloaded with your account.";
    else if (message == "TOO_SOON_AFTER_DOWNLOAD")
        r.message = "Nexus requires some time between downloading and endorsing - "
                    "try again later from the mod page.";
    else if (message == "IS_OWN_MOD")
        r.message = "You can't endorse your own mod.";
    // Success if the status reflects the requested state, or there's no error-like
    // message. Nexus errors come back as uppercase codes (NOT_DOWNLOADED_MOD,
    // TOO_SOON_AFTER_DOWNLOAD, ...) in the message field.
    const bool statusOk = (status.compare("Endorsed", Qt::CaseInsensitive) == 0
                           || status.compare("Abstained", Qt::CaseInsensitive) == 0);
    const bool errorLike = message.contains('_') && message == message.toUpper() && !message.isEmpty();
    r.ok = statusOk || (!errorLike && !message.isEmpty() && message != "Error");
    return r;
}

// Shared track/untrack worker. post/DELETE /v1/user/tracked_mods.json with the
// game domain as a query param and mod_id in the form body. Any 2xx (a fresh
// track is 201, an already-tracked mod is 200) leaves curlRun's err empty -> ok.
static NexusApi::TrackResult setTracked(const QString& modId, const QString& game, bool track) {
    NexusApi::TrackResult r;
    if (modId.isEmpty()) { r.message = "No Nexus mod id."; return r; }
    qCInfo(lcNexus) << (track ? "track" : "untrack") << game << "mod" << modId;
    QUrl url("https://api.nexusmods.com/v1/user/tracked_mods.json");
    QUrlQuery q; q.addQueryItem("domain_name", game);
    url.setQuery(q);
    QStringList args;
    args << "-X" << (track ? "POST" : "DELETE")
         << "-d" << ("mod_id=" + modId)
         << url.toString(QUrl::FullyEncoded);
    QString err;
    const QByteArray body = curlRun(args, &err);
    if (!err.isEmpty()) { r.message = err; return r; }
    r.ok = true; // 2xx; body may be empty or a {"message": ...} note
    const auto doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) r.message = doc.object()["message"].toString();
    return r;
}

NexusApi::TrackResult NexusApi::track(const QString& modId, const QString& game) {
    return setTracked(modId, game, true);
}

NexusApi::TrackResult NexusApi::untrack(const QString& modId, const QString& game) {
    return setTracked(modId, game, false);
}

NexusApi::Md5Match NexusApi::md5Search(const QString& md5, const QString& game) {
    Md5Match r;
    if (md5.isEmpty()) return r;
    qCInfo(lcNexus) << "md5_search" << game << "md5" << md5;
    QString url = kBase + game + "/mods/md5_search/" + md5 + ".json";
    QByteArray body = curlGet(url);
    if (body.isEmpty()) { qCWarning(lcNexus) << "md5_search: empty response for md5" << md5; return r; }
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
    qCInfo(lcNexus) << "fileVersion" << game << "mod" << modId << "file" << fileId;
    QString url = kBase + game + "/mods/" + modId + "/files/" + fileId + ".json";
    QByteArray body = curlGet(url);
    if (body.isEmpty()) { qCWarning(lcNexus) << "fileVersion: empty response for mod" << modId << "file" << fileId; return {}; }
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
    qCInfo(lcNexus) << "list" << list << game;
    QByteArray body = curlGet(kBase + game + "/mods/" + list + ".json");
    if (body.isEmpty()) { qCWarning(lcNexus) << "list: empty response for" << list; return out; }
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
    qCInfo(lcNexus) << "graphql search" << "gameId" << gameId << "count" << n;
    QByteArray body = curlPostJson("https://api.nexusmods.com/v2/graphql", reqBody);
    if (body.isEmpty()) { qCWarning(lcNexus) << "search: empty response"; return out; }
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
    qCInfo(lcNexus) << "modDetails" << game << "mod" << modId;
    QByteArray body = curlGet(kBase + game + "/mods/" + modId + ".json");
    if (body.isEmpty()) { qCWarning(lcNexus) << "modDetails: empty response for mod" << modId; return r; }
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
    qCInfo(lcNexus) << "files" << game << "mod" << modId;
    QByteArray body = curlGet(kBase + game + "/mods/" + modId + "/files.json");
    if (body.isEmpty()) { qCWarning(lcNexus) << "files: empty response for mod" << modId; return out; }
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

QList<NexusApi::ModRequirement> NexusApi::modRequirements(const QString& modId, const QString& game) {
    QList<ModRequirement> out;
    if (modId.isEmpty()) return out;
    const QString gameId = numericGameId(game);
    const QString q = QString(
        "query{mod(modId:\"%1\",gameId:\"%2\"){modRequirements{"
        "nexusRequirements{nodes{modId modName externalRequirement notes url}}}}}")
        .arg(modId, gameId);
    QJsonObject reqObj; reqObj["query"] = q;
    QByteArray reqBody = QJsonDocument(reqObj).toJson(QJsonDocument::Compact);
    qCInfo(lcNexus) << "graphql modRequirements" << game << "mod" << modId;
    QByteArray body = curlPostJson("https://api.nexusmods.com/v2/graphql", reqBody);
    if (body.isEmpty()) { qCWarning(lcNexus) << "modRequirements: empty response for mod" << modId; return out; }
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) return out;
    auto root = doc.object();
    if (root.contains("errors")) return out;   // not-found / malformed query
    // modId may serialize as a JSON number or a string; accept either.
    auto idOf = [](const QJsonObject& o) -> QString {
        const QJsonValue v = o["modId"];
        if (v.isString()) return v.toString();
        int n = v.toInt();
        return n > 0 ? QString::number(n) : QString();
    };
    auto nodes = root["data"].toObject()["mod"].toObject()
                     ["modRequirements"].toObject()
                     ["nexusRequirements"].toObject()["nodes"].toArray();
    for (const auto& v : nodes) {
        auto o = v.toObject();
        ModRequirement r;
        r.modId    = idOf(o);
        r.modName  = o["modName"].toString();
        r.external = o["externalRequirement"].toBool();
        r.notes    = o["notes"].toString();
        r.url      = o["url"].toString();
        if (r.external) {
            // Off-site requirement: modId is a meaningless "0". Keep it only if it
            // carries something to show/click (url/name/notes), else it's useless.
            if (r.url.isEmpty() && r.modName.isEmpty() && r.notes.isEmpty()) continue;
        } else {
            // A Nexus requirement needs a real mod id ("0"/empty is not installable).
            if (r.modId.isEmpty() || r.modId == "0") continue;
        }
        out.append(r);
    }
    return out;
}

QString NexusApi::downloadUrl(const QString& modId, const QString& fileId, const QString& game) {
    if (modId.isEmpty() || fileId.isEmpty()) return {};
    qCInfo(lcNexus) << "downloadUrl" << game << "mod" << modId << "file" << fileId;
    QByteArray body = curlGet(kBase + game + "/mods/" + modId + "/files/" + fileId + "/download_link.json");
    if (body.isEmpty()) { qCWarning(lcNexus) << "downloadUrl: empty response for mod" << modId << "file" << fileId; return {}; }
    auto doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) { qCWarning(lcNexus) << "downloadUrl: non-array response (non-premium?) for mod" << modId << "file" << fileId; return {}; }   // non-premium returns an error object
    const QJsonArray arr = doc.array();
    if (arr.isEmpty()) return {};
    AppConfig::instance().setCachedDownloadServers(mirrorServerNames(arr));
    return pickMirror(arr, AppConfig::instance().preferredDownloadServer());
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
    args << "-s" << "--max-time" << "10" << "-K" << "-"
         << "https://api.nexusmods.com/v1/users/validate.json";
    qCInfo(lcNexus) << "validateUser";
    const auto hc = solero::hostCommand("curl", args, {}, solero::runningInFlatpak());
    p.start(hc.program, hc.args);
    // Supply the apikey header via stdin config, never argv.
    p.write(QByteArray("header = \"apikey: ") + useKey.toUtf8() + "\"\n");
    p.closeWriteChannel();
    p.waitForFinished(15000);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qCWarning(lcNexus) << "validateUser: curl failed, exit code" << p.exitCode();
        return r;
    }
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
    const QString path = apiKeyPath();
    if (!atomicWrite(path, key.trimmed().toUtf8())) return false;
    // The key is a secret: keep it owner-only (0600), not the default 0644.
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void NexusApi::clearApiKey() {
    QFile::remove(apiKeyPath());
}

} // namespace solero
