#pragma once
#include <QObject>
class QLocalServer;
namespace solero {
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject* parent = nullptr);
    // Returns true if another instance already owns `key` (i.e. we connected to it).
    static bool isAnotherRunning(const QString& key);
    // Connect to the running instance and send a one-line message.
    static bool sendToRunning(const QString& key, const QString& msg);
    // Become the owner: listen on `key`. Returns true on success.
    bool listen(const QString& key);
signals:
    void messageReceived(const QString& msg);
private:
    QLocalServer* m_server = nullptr;
};
}
