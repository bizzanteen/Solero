#include "app/SingleInstance.h"
#include <QLocalServer>
#include <QLocalSocket>

namespace solero {

SingleInstance::SingleInstance(QObject* parent) : QObject(parent) {}

bool SingleInstance::isAnotherRunning(const QString& key) {
    QLocalSocket s;
    s.connectToServer(key);
    bool ok = s.waitForConnected(200);
    if (ok) s.disconnectFromServer();
    return ok;
}

bool SingleInstance::sendToRunning(const QString& key, const QString& msg) {
    QLocalSocket s;
    s.connectToServer(key);
    bool connected = s.waitForConnected(500);
    if (connected) {
        s.write(msg.toUtf8() + "\n");
        s.flush();
        s.waitForBytesWritten(500);
        s.disconnectFromServer();
    }
    return connected;
}

bool SingleInstance::listen(const QString& key) {
    QLocalServer::removeServer(key); // clear stale socket
    m_server = new QLocalServer(this);
    bool ok = m_server->listen(key);
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        auto* c = m_server->nextPendingConnection();
        connect(c, &QLocalSocket::readyRead, this, [this, c] {
            QByteArray d = c->readAll();
            for (const auto& line : QString::fromUtf8(d).split('\n', Qt::SkipEmptyParts))
                emit messageReceived(line.trimmed());
        });
        connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
    });
    return ok;
}

} // namespace solero
