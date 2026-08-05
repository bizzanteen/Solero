#pragma once
#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>

namespace solero { class Profile; class AITransactionLog; }

namespace solero {

class IPCServer : public QObject {
    Q_OBJECT
public:
    explicit IPCServer(QObject* parent = nullptr);
    bool start(const QString& socketName = "solero-ipc");

    void setActiveProfile(Profile* profile) { m_profile = profile; }
    void setTransactionLog(AITransactionLog* log) { m_txLog = log; }

signals:
    // A request changed the active profile in memory and on disk. sort_plugins
    // replaces the whole plugin list, so any view still showing the old one has
    // to be rebuilt or it will write a stale order back on the next save.
    void profileMutated();

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QLocalServer* m_server = nullptr;
    Profile* m_profile = nullptr;
    AITransactionLog* m_txLog = nullptr;

    QByteArray handleRequest(const QByteArray& requestJson);
    QByteArray handleListMods(const QJsonObject& req);
    QByteArray handleListPlugins(const QJsonObject& req);
    QByteArray handleListProfiles(const QJsonObject& req);
    QByteArray handleGetSummary(const QJsonObject& req);
    QByteArray handleAIWrite(const QJsonObject& req);
    QByteArray handleEnableMod(const QJsonObject& req);
    QByteArray handleMoveMod(const QJsonObject& req);
    QByteArray handleAIRevert(const QJsonObject& req);
    QByteArray handleListTransactions(const QJsonObject& req);
    QByteArray handleListLootRules(const QJsonObject& req);
    QByteArray handleSetLootRule(const QJsonObject& req);
    QByteArray handleDeleteLootRule(const QJsonObject& req);
    QByteArray handleSortPlugins(const QJsonObject& req);

    // Shared by set_loot_rule / delete_loot_rule: parse and check the request,
    // then write the edited userlist inside a revertable transaction.
    QByteArray editLootRule(const QJsonObject& req, bool removing);

    static QByteArray ok(const QJsonObject& data);
    static QByteArray err(const QString& message);
};

} // namespace solero
