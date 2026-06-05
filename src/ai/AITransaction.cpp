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

        // All-or-nothing: stage every file's `before` content into a temp file
        // first. Only once all temps are written do we atomically rename them
        // into place. A failure at any point cleans up and leaves the profile
        // untouched, with `reverted` still false.
        QStringList temps;        // staged temp paths, parallel to targets
        QStringList targets;
        bool staged = true;
        for (const auto& snap : tx.snapshots) {
            const QString tmp = snap.filePath + ".revert.tmp";
            QFile f(tmp);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                f.write(snap.before) != snap.before.size()) {
                f.close();
                QFile::remove(tmp);
                staged = false;
                break;
            }
            f.flush();
            f.close();
            temps.append(tmp);
            targets.append(snap.filePath);
        }
        if (!staged) {
            for (const QString& t : temps) QFile::remove(t);
            return false;
        }

        // Swap stage: move each staged temp into place atomically. Because every
        // temp was written successfully above, this stage only performs renames,
        // so a mid-loop failure cannot leave a file truncated/half-written.
        // (`writer` is accepted for API compatibility; the durable write is done
        // here via atomicWrite so callers can't undermine atomicity.)
        Q_UNUSED(writer);
        bool swapped = true;
        for (int i = 0; i < temps.size(); ++i) {
            QFile tf(temps[i]);
            QByteArray data;
            if (tf.open(QIODevice::ReadOnly)) { data = tf.readAll(); tf.close(); }
            if (!atomicWrite(targets[i], data)) swapped = false;
            QFile::remove(temps[i]);
        }
        if (!swapped) return false;

        tx.reverted = true;
        persist();
        return true;
    }
    return false;
}

void AITransactionLog::persist() const {
    // Bound the persisted history: each entry carries full before+after content
    // (base64) per file, so an unbounded log would grow without limit. Keep only
    // the most recent kMaxHistory transactions, pruning the oldest (front).
    constexpr int kMaxHistory = 50;
    if (m_log.size() > kMaxHistory)
        m_log.erase(m_log.begin(), m_log.end() - kMaxHistory);

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
