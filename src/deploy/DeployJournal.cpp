#include "DeployJournal.h"
#include "DeployRecord.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace solero {

DeployJournal::~DeployJournal() = default;

QString DeployJournal::pathFor(const QString& recordPath) {
    return recordPath + QStringLiteral(".partial");
}

bool DeployJournal::begin(const QString& recordPath) {
    m_path = pathFor(recordPath);
    m_file = std::make_unique<QFile>(m_path);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_file.reset();
        return false;
    }
    return true;
}

void DeployJournal::append(const QString& relPath, const QString& modId,
                           qint64 size, qint64 mtimeMs) {
    if (!m_file) return;
    QJsonObject o;
    o.insert(QStringLiteral("p"), relPath);
    o.insert(QStringLiteral("m"), modId);
    // Mirror DeployRecord: a size below zero means "no fingerprint" (generated
    // artifacts), so leave the pair out entirely rather than storing -1.
    if (size >= 0) {
        o.insert(QStringLiteral("s"), double(size));
        o.insert(QStringLiteral("t"), double(mtimeMs));
    }
    m_file->write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    m_file->write("\n");
}

void DeployJournal::flush() {
    if (m_file) m_file->flush();
}

void DeployJournal::discard() {
    if (m_file) {
        m_file->close();
        m_file.reset();
    }
    if (!m_path.isEmpty()) {
        QFile::remove(m_path);
        m_path.clear();
    }
}

void DeployJournal::mergeInto(DeployRecord& record, const QString& recordPath) {
    QFile f(pathFor(recordPath));
    if (!f.open(QIODevice::ReadOnly)) return;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        const QString relPath = o.value(QStringLiteral("p")).toString();
        if (relPath.isEmpty()) continue;   // truncated tail of an interrupted deploy
        const QString modId = o.value(QStringLiteral("m")).toString();
        if (o.contains(QStringLiteral("s")))
            record.add(relPath, modId,
                       qint64(o.value(QStringLiteral("s")).toDouble()),
                       qint64(o.value(QStringLiteral("t")).toDouble()));
        else
            record.add(relPath, modId);
    }
}

} // namespace solero
