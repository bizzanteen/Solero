#pragma once
#include "core/Types.h"
#include <QList>
#include <QString>
#include <functional>

namespace solero {

class AITransactionLog {
public:
    explicit AITransactionLog(const QString& logPath);

    QString beginTransaction(const QString& description,
                              const QStringList& filesToSnapshot,
                              std::function<QByteArray(const QString&)> fileReader);

    void commitTransaction(const QString& txId,
                            std::function<QByteArray(const QString&)> fileReader);

    bool revertTransaction(const QString& txId,
                            std::function<bool(const QString&, const QByteArray&)> fileWriter);

    const QList<AITransaction>& transactions() const { return m_log; }

private:
    QString m_logPath;
    // Mutable so persist() (const) can prune the oldest entries to bound history.
    mutable QList<AITransaction> m_log;
    void persist() const;
    void loadFromDisk();
    static QString newUuid();
};

} // namespace solero
