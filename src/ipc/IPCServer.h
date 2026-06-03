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

    static QByteArray ok(const QJsonObject& data);
    static QByteArray err(const QString& message);
};

} // namespace solero
