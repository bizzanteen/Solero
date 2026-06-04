#include "DeployRecord.h"
#include "core/FileUtil.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace solero {

void DeployRecord::add(const QString& relPath, const QString& modId) {
    m_files.insert(relPath, modId);
}

void DeployRecord::remove(const QString& relPath) {
    m_files.remove(relPath);
}

void DeployRecord::clear() {
    m_files.clear();
}

QString DeployRecord::ownerOf(const QString& relPath) const {
    return m_files.value(relPath);
}

QStringList DeployRecord::allPaths() const {
    return m_files.keys();
}

bool DeployRecord::saveToFile(const QString& path) const {
    QJsonObject obj;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it)
        obj.insert(it.key(), it.value());
    return atomicWrite(path, QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

DeployRecord DeployRecord::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    DeployRecord rec;
    auto obj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        rec.add(it.key(), it.value().toString());
    return rec;
}

} // namespace solero
