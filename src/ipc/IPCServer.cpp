#include "IPCServer.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include "core/FileUtil.h"
#include "core/LoadOrderBackup.h"
#include "ai/AITransaction.h"
#include "loot/LootSorter.h"
#include "loot/LootUserlist.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLocalSocket>
#include <QFile>

namespace solero {

namespace {
// Every handler that snapshots or restores a file uses these.
QByteArray readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
} // namespace

IPCServer::IPCServer(QObject* parent) : QObject(parent) {
    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection, this, &IPCServer::onNewConnection);
}

bool IPCServer::start(const QString& socketName) {
    QLocalServer::removeServer(socketName);
    // Restrict the socket to the current user before listening so other local
    // users cannot connect to the IPC channel.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
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
    // Guard against a peer that floods bytes without ever sending a newline:
    // QLocalSocket would buffer those bytes unbounded. If the pending (unframed)
    // bytes exceed the cap, drop the connection.
    constexpr qint64 kMaxBuffered = 1 * 1024 * 1024; // 1 MiB
    if (socket->bytesAvailable() > kMaxBuffered) {
        socket->abort();
        socket->deleteLater();
        return;
    }
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
    if (action == "list_loot_rules")    return handleListLootRules(req);
    if (action == "set_loot_rule")      return handleSetLootRule(req);
    if (action == "delete_loot_rule")   return handleDeleteLootRule(req);
    if (action == "sort_plugins")       return handleSortPlugins(req);
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

    QString txId = m_txLog->beginTransaction(description, files, readFile);

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

    m_txLog->commitTransaction(txId, readFile);

    emit profileMutated();

    QJsonObject res; res["transactionId"] = txId;
    return ok(res);
}

QByteArray IPCServer::handleEnableMod(const QJsonObject& req) {
    if (!m_profile || !m_txLog) return err("no active profile or tx log");
    QString modId   = req["modId"].toString();
    bool    enabled = req["enabled"].toBool(true);
    QStringList files = { m_profile->modlistPath() };
    QString txId = m_txLog->beginTransaction(
        QString("%1 mod: %2").arg(enabled ? "Enable" : "Disable", modId), files, readFile);
    m_profile->modList().setEnabled(modId, enabled);
    m_profile->save();
    m_txLog->commitTransaction(txId, readFile);
    emit profileMutated();
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
    QString txId = m_txLog->beginTransaction(
        QString("Move mod from %1 to %2").arg(from).arg(to), files, readFile);
    m_profile->modList().move(from, to);
    m_profile->save();
    m_txLog->commitTransaction(txId, readFile);
    emit profileMutated();
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
    emit profileMutated();
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

QByteArray IPCServer::handleListLootRules(const QJsonObject&) {
    if (!m_profile) return err("no active profile");
    const QString path = m_profile->lootUserlistPath();
    const QString yaml = QString::fromUtf8(readFile(path));

    QJsonArray plugins;
    for (const auto& p : LootUserlist::parsePlugins(yaml)) {
        QJsonObject o;
        o["name"]  = p.name;
        o["after"] = QJsonArray::fromStringList(p.after);
        o["req"]   = QJsonArray::fromStringList(p.req);
        o["inc"]   = QJsonArray::fromStringList(p.inc);
        o["group"] = p.group;
        plugins.append(o);
    }

    QJsonObject res;
    res["userlistPath"]   = path;
    res["exists"]         = QFile::exists(path);
    res["plugins"]        = plugins;
    res["userlistGroups"] = QJsonArray::fromStringList(LootUserlist::parseGroups(yaml));

    // The names a group rule may legally use - mostly masterlist ones, so this
    // has to come from libloot rather than the file above.
    const auto groups = LootSorter::groups(AppConfig::instance().gameDir(), path);
    if (groups.success) res["availableGroups"] = QJsonArray::fromStringList(groups.names);
    else                res["groupsError"]     = groups.errorMessage;
    return ok(res);
}

QByteArray IPCServer::handleSetLootRule(const QJsonObject& req)    { return editLootRule(req, false); }
QByteArray IPCServer::handleDeleteLootRule(const QJsonObject& req) { return editLootRule(req, true); }

QByteArray IPCServer::editLootRule(const QJsonObject& req, bool removing) {
    if (!m_profile || !m_txLog) return err("no active profile or tx log");
    const QString plugin   = req["plugin"].toString().trimmed();
    const QString typeName = req["type"].toString().trimmed();
    const QString target   = req["target"].toString().trimmed();
    if (plugin.isEmpty()) return err("plugin is required");
    if (target.isEmpty()) return err("target is required");

    LootRuleType kind = LootRuleType::After;
    if (!LootUserlist::ruleTypeFromName(typeName, &kind))
        return err(QString("unknown rule type \"%1\" - use after, req, inc or group")
                       .arg(typeName));

    const QString path = m_profile->lootUserlistPath();

    // Check a group assignment BEFORE it reaches the file. An undefined group
    // makes libloot abort the whole sort, not just skip the rule, so a typo
    // here silently leaves the entire load order unsorted.
    if (kind == LootRuleType::Group && !removing) {
        const auto groups = LootSorter::groups(AppConfig::instance().gameDir(), path);
        if (!groups.success)
            return err(QString("the LOOT groups could not be read, so \"%1\" can't be "
                               "verified and the rule was not written: %2")
                           .arg(target, groups.errorMessage));
        const QString problem = LootUserlist::validateGroupName(target, groups.names);
        if (!problem.isEmpty()) return err(problem);
    }

    const QString before = QString::fromUtf8(readFile(path));
    bool removed = false;
    const QString after = removing
        ? LootUserlist::removeRule(before, plugin, kind, target, &removed)
        : LootUserlist::addRule(before, plugin, kind, target);

    QJsonObject res;
    res["plugin"] = plugin;
    res["type"]   = LootUserlist::ruleTypeName(kind);
    res["target"] = target;

    if (removing && !removed) {
        res["changed"] = false;
        res["note"]    = "no such rule - nothing to remove";
        return ok(res);
    }
    if (after == before) {
        res["changed"] = false;
        res["note"]    = "the rule was already there";
        return ok(res);
    }

    const QString description = QString("%1 LOOT rule: %2 %3 %4")
        .arg(removing ? "Remove" : "Set", plugin, LootUserlist::ruleTypeName(kind), target);
    const QString txId = m_txLog->beginTransaction(description, {path}, readFile);
    if (!atomicWrite(path, after.toUtf8()))
        return err("could not write " + path);
    m_txLog->commitTransaction(txId, readFile);

    res["changed"]       = true;
    res["transactionId"] = txId;
    res["userlistPath"]  = path;
    // Rules only take effect on the next sort, and saving one does not trigger
    // it. Say so, so a caller doesn't assume the load order just moved.
    res["note"] = "written - run sort_plugins to apply it";

    // A rule naming a plugin that isn't in the load order is legal but inert;
    // most of the time it means a typo.
    if (!m_profile->pluginList().findByFilename(plugin))
        res["warning"] = QString("\"%1\" is not in this profile's load order").arg(plugin);
    return ok(res);
}

QByteArray IPCServer::handleSortPlugins(const QJsonObject&) {
    if (!m_profile) return err("no active profile");
    if (m_profile->pluginList().loadOrderLocked())
        return err("the load order is locked - unlock it in Solero before sorting");

    const QString gameDir = AppConfig::instance().gameDir();
    if (gameDir.isEmpty()) return err("no game directory is configured");

    // libloot reads plugin headers out of the game's Data folder, so an
    // undeployed profile sorts "successfully" without moving anything.
    const QString dataDir = gameDir + "/Data";
    int onDisk = 0;
    for (int i = 0; i < m_profile->pluginList().count(); ++i)
        if (QFile::exists(dataDir + "/" + m_profile->pluginList().at(i).filename)) ++onDisk;
    if (onDisk == 0)
        return err("none of this profile's plugins are in " + dataDir +
                   " - deploy in Solero first, then sort");

    LoadOrderBackup::create(*m_profile, QString("Auto (pre-LOOT %1)")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")));

    auto result = LootSorter::sort(m_profile->pluginList(), gameDir,
                                   m_profile->lootUserlistPath());
    if (!result.success)
        return err(result.errorMessage.isEmpty()
                       ? QStringLiteral("LOOT could not sort the plugins.")
                       : result.errorMessage);

    m_profile->pluginList().applyPins();
    m_profile->save();
    emit profileMutated();

    QJsonArray order;
    for (int i = 0; i < m_profile->pluginList().count(); ++i)
        order.append(m_profile->pluginList().at(i).filename);

    QJsonObject res;
    res["sorted"]        = true;
    res["pluginsOnDisk"] = onDisk;
    res["order"]         = order;
    if (!result.warnings.isEmpty())
        res["warnings"] = QJsonArray::fromStringList(result.warnings);
    // The sorted order is in the profile now; it reaches the game on the next
    // deploy or launch.
    res["note"] = "sorted - deploy or launch in Solero to push the order to the game";
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
