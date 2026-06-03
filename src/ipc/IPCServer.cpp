#include "IPCServer.h"
#include "core/Profile.h"
#include "ai/AITransaction.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLocalSocket>
#include <QFile>

namespace solero {

IPCServer::IPCServer(QObject* parent) : QObject(parent) {
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection, this, &IPCServer::onNewConnection);
}

bool IPCServer::start(const QString& socketName) {
    QLocalServer::removeServer(socketName);
    return m_server->listen(socketName);
}

void IPCServer::onNewConnection() {
    auto* socket = m_server->nextPendingConnection();
    socket->setParent(this);
    connect(socket, &QLocalSocket::readyRead, this, &IPCServer::onReadyRead);
    connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
}

void IPCServer::onReadyRead() {
    auto* socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket) return;
    while (socket->canReadLine()) {
        QByteArray line = socket->readLine().trimmed();
        QByteArray response = handleRequest(line);
        socket->write(response + "\n");
        socket->flush();
    }
}

QByteArray IPCServer::handleRequest(const QByteArray& json) {
    QJsonParseError e;
    auto doc = QJsonDocument::fromJson(json, &e);
    if (e.error != QJsonParseError::NoError) return err("invalid json");
    auto req = doc.object();
    QString action = req["action"].toString();
    if (action == "list_mods")          return handleListMods(req);
    if (action == "list_plugins")       return handleListPlugins(req);
    if (action == "list_profiles")      return handleListProfiles(req);
    if (action == "get_summary")        return handleGetSummary(req);
    if (action == "ai_write")           return handleAIWrite(req);
    if (action == "enable_mod")         return handleEnableMod(req);
    if (action == "move_mod")           return handleMoveMod(req);
    if (action == "ai_revert")          return handleAIRevert(req);
    if (action == "list_transactions")  return handleListTransactions(req);
    return err("unknown action: " + action);
}

QByteArray IPCServer::handleListMods(const QJsonObject&) {
    if (!m_profile) return err("no active profile");
    QJsonArray arr;
    for (const auto& e : m_profile->modList()) {
        QJsonObject o;
        o["id"]      = e.id;
        o["name"]    = e.name;
        o["type"]    = (e.type == EntryType::Mod) ? "mod" : "separator";
        o["enabled"] = e.enabled;
        o["version"] = e.version;
        arr.append(o);
    }
    QJsonObject res; res["mods"] = arr;
    return ok(res);
}

QByteArray IPCServer::handleListPlugins(const QJsonObject&) {
    if (!m_profile) return err("no active profile");
    QJsonArray arr;
    for (int i = 0; i < m_profile->pluginList().count(); ++i) {
        const auto& p = m_profile->pluginList().at(i);
        QJsonObject o;
        o["filename"] = p.filename;
        o["enabled"]  = p.enabled;
        o["priority"] = i;
        o["isMaster"] = p.isMaster;
        o["isLight"]  = p.isLight;
        arr.append(o);
    }
    QJsonObject res; res["plugins"] = arr;
    return ok(res);
}

QByteArray IPCServer::handleListProfiles(const QJsonObject&) {
    QJsonObject res;
    res["activeProfile"] = m_profile ? m_profile->name() : QString();
    return ok(res);
}

QByteArray IPCServer::handleGetSummary(const QJsonObject&) {
    if (!m_profile) return err("no active profile");
    QJsonObject res;
    res["profile"]     = m_profile->name();
    res["modCount"]    = m_profile->modList().count();
    res["pluginCount"] = m_profile->pluginList().count();
    return ok(res);
}

QByteArray IPCServer::handleAIWrite(const QJsonObject& req) {
    if (!m_profile || !m_txLog) return err("no active profile or tx log");
    QString description = req["description"].toString("AI change");
    QStringList files = { m_profile->modlistPath(), m_profile->pluginsPath() };

    QString txId = m_txLog->beginTransaction(description, files,
        [](const QString& path) -> QByteArray {
            QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll();
        });

    auto changes = req["changes"].toArray();
    for (const auto& cv : changes) {
        auto c = cv.toObject();
        QString type = c["type"].toString();
        if (type == "set_mod_enabled") {
            m_profile->modList().setEnabled(c["modId"].toString(), c["enabled"].toBool());
        } else if (type == "move_plugin") {
            m_profile->pluginList().move(c["from"].toInt(), c["to"].toInt());
        }
    }
    m_profile->save();

    m_txLog->commitTransaction(txId,
        [](const QString& path) -> QByteArray {
            QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll();
        });

    QJsonObject res; res["transactionId"] = txId;
    return ok(res);
}

QByteArray IPCServer::handleEnableMod(const QJsonObject& req) {
    if (!m_profile || !m_txLog) return err("no active profile or tx log");
    QString modId   = req["modId"].toString();
    bool    enabled = req["enabled"].toBool(true);
    QStringList files = { m_profile->modlistPath() };
    auto reader = [](const QString& p) -> QByteArray {
        QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll();
    };
    QString txId = m_txLog->beginTransaction(
        QString("%1 mod: %2").arg(enabled ? "Enable" : "Disable", modId), files, reader);
    m_profile->modList().setEnabled(modId, enabled);
    m_profile->save();
    m_txLog->commitTransaction(txId, reader);
    QJsonObject res;
    res["transactionId"] = txId;
    res["modId"] = modId;
    res["enabled"] = enabled;
    return ok(res);
}

QByteArray IPCServer::handleMoveMod(const QJsonObject& req) {
    if (!m_profile || !m_txLog) return err("no active profile or tx log");
    int from = req["from"].toInt(-1);
    int to   = req["to"].toInt(-1);
    if (from < 0 || to < 0) return err("from and to must be non-negative integers");
    QStringList files = { m_profile->modlistPath() };
    auto reader = [](const QString& p) -> QByteArray {
        QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll();
    };
    QString txId = m_txLog->beginTransaction(
        QString("Move mod from %1 to %2").arg(from).arg(to), files, reader);
    m_profile->modList().move(from, to);
    m_profile->save();
    m_txLog->commitTransaction(txId, reader);
    QJsonObject res;
    res["transactionId"] = txId;
    res["from"] = from;
    res["to"] = to;
    return ok(res);
}

QByteArray IPCServer::handleAIRevert(const QJsonObject& req) {
    if (!m_txLog) return err("no tx log");
    QString txId = req["transactionId"].toString();
    bool success = m_txLog->revertTransaction(txId,
        [](const QString& path, const QByteArray& content) -> bool {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) return false;
            f.write(content);
            return true;
        });
    if (!success) return err("revert failed or already reverted");
    if (m_profile) m_profile->load();
    QJsonObject res; res["reverted"] = true;
    return ok(res);
}

QByteArray IPCServer::handleListTransactions(const QJsonObject&) {
    if (!m_txLog) return err("no tx log");
    QJsonArray arr;
    for (const auto& tx : m_txLog->transactions()) {
        QJsonObject o;
        o["id"]          = tx.id;
        o["timestamp"]   = tx.timestamp.toString(Qt::ISODate);
        o["description"] = tx.description;
        o["reverted"]    = tx.reverted;
        arr.append(o);
    }
    QJsonObject res; res["transactions"] = arr;
    return ok(res);
}

QByteArray IPCServer::ok(const QJsonObject& data) {
    QJsonObject r = data; r["ok"] = true;
    return QJsonDocument(r).toJson(QJsonDocument::Compact);
}

QByteArray IPCServer::err(const QString& msg) {
    QJsonObject r; r["ok"] = false; r["error"] = msg;
    return QJsonDocument(r).toJson(QJsonDocument::Compact);
}

} // namespace solero
