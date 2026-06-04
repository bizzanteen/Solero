#include "AITransaction.h"
#include "core/FileUtil.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

namespace solero {

AITransactionLog::AITransactionLog(const QString& logPath) : m_logPath(logPath) {
    loadFromDisk();
}

QString AITransactionLog::newUuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString AITransactionLog::beginTransaction(const QString& description,
                                            const QStringList& files,
                                            std::function<QByteArray(const QString&)> reader) {
    AITransaction tx;
    tx.id          = newUuid();
    tx.timestamp   = QDateTime::currentDateTimeUtc();
    tx.description = description;
    tx.reverted    = false;
    for (const auto& path : files) {
        FileSnapshot snap;
        snap.filePath = path;
        snap.before   = reader(path);
        tx.snapshots.append(snap);
    }
    m_log.append(tx);
    persist();
    return tx.id;
}

void AITransactionLog::commitTransaction(const QString& txId,
                                          std::function<QByteArray(const QString&)> reader) {
    for (auto& tx : m_log) {
        if (tx.id != txId) continue;
        for (auto& snap : tx.snapshots)
            snap.after = reader(snap.filePath);
        break;
    }
    persist();
}

bool AITransactionLog::revertTransaction(const QString& txId,
                                          std::function<bool(const QString&, const QByteArray&)> writer) {
    for (auto& tx : m_log) {
        if (tx.id != txId || tx.reverted) continue;
        for (const auto& snap : tx.snapshots) {
            if (!writer(snap.filePath, snap.before)) return false;
        }
        tx.reverted = true;
        persist();
        return true;
    }
    return false;
}

void AITransactionLog::persist() const {
    QJsonArray arr;
    for (const auto& tx : m_log) {
        QJsonObject o;
        o["id"]          = tx.id;
        o["timestamp"]   = tx.timestamp.toString(Qt::ISODate);
        o["description"] = tx.description;
        o["reverted"]    = tx.reverted;
        QJsonArray snaps;
        for (const auto& s : tx.snapshots) {
            QJsonObject so;
            so["filePath"] = s.filePath;
            so["before"]   = QString::fromLatin1(s.before.toBase64());
            so["after"]    = QString::fromLatin1(s.after.toBase64());
            snaps.append(so);
        }
        o["snapshots"] = snaps;
        arr.append(o);
    }
    atomicWrite(m_logPath, QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

void AITransactionLog::loadFromDisk() {
    QFile f(m_logPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
        auto o = v.toObject();
        AITransaction tx;
        tx.id          = o["id"].toString();
        tx.timestamp   = QDateTime::fromString(o["timestamp"].toString(), Qt::ISODate);
        tx.description = o["description"].toString();
        tx.reverted    = o["reverted"].toBool(false);
        for (const auto& sv : o["snapshots"].toArray()) {
            auto so = sv.toObject();
            FileSnapshot s;
            s.filePath = so["filePath"].toString();
            s.before   = QByteArray::fromBase64(so["before"].toString().toLatin1());
            s.after    = QByteArray::fromBase64(so["after"].toString().toLatin1());
            tx.snapshots.append(s);
        }
        m_log.append(tx);
    }
}

} // namespace solero
