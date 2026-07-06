#include "DeployRecord.h"
#include "core/FileUtil.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace solero {

void DeployRecord::add(const QString& relPath, const QString& modId) {
    m_files.insert(relPath, Entry{modId, -1, -1});
}

void DeployRecord::add(const QString& relPath, const QString& modId,
                       qint64 size, qint64 mtimeMs) {
    m_files.insert(relPath, Entry{modId, size, mtimeMs});
}

void DeployRecord::remove(const QString& relPath) {
    m_files.remove(relPath);
}

void DeployRecord::clear() {
    m_files.clear();
}

QString DeployRecord::ownerOf(const QString& relPath) const {
    auto it = m_files.constFind(relPath);
    return it == m_files.constEnd() ? QString() : it.value().modId;
}

DeployRecord::Fingerprint DeployRecord::fingerprintOf(const QString& relPath) const {
    auto it = m_files.constFind(relPath);
    if (it == m_files.constEnd()) return {};
    return Fingerprint{it.value().size, it.value().mtimeMs};
}

bool DeployRecord::contains(const QString& relPath) const {
    return m_files.contains(relPath);
}

QStringList DeployRecord::allPaths() const {
    return m_files.keys();
}

bool DeployRecord::saveToFile(const QString& path) const {
    QJsonObject files;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it) {
        const Entry& e = it.value();
        QJsonObject rec;
        rec.insert("mod", e.modId);
        // Only emit a fingerprint when one was captured (real staged files). A
        // generated artifact (size < 0) stores just its owner.
        if (e.size >= 0) {
            rec.insert("size", double(e.size));
            rec.insert("mtime", double(e.mtimeMs));
        }
        files.insert(it.key(), rec);
    }
    QJsonObject obj;
    obj.insert("version", 2);
    obj.insert("mode", m_mode);
    obj.insert("files", files);
    return atomicWrite(path, QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

DeployRecord DeployRecord::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    DeployRecord rec;
    const auto obj = QJsonDocument::fromJson(f.readAll()).object();

    // v2 records wrap the paths in a "files" object alongside version/mode. A
    // legacy v1 record is a flat map of relPath -> "modId" strings; detect it by
    // the absence of a "files" OBJECT (a v1 path literally named "files" would map
    // to a string, not an object, so this stays robust).
    if (obj.value("files").isObject()) {
        rec.m_version = 2;
        rec.m_mode = obj.value("mode").toInt(-1);
        const QJsonObject files = obj.value("files").toObject();
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            const QJsonObject e = it.value().toObject();
            const QString modId = e.value("mod").toString();
            if (e.contains("size"))
                rec.add(it.key(), modId,
                        qint64(e.value("size").toDouble()),
                        qint64(e.value("mtime").toDouble()));
            else
                rec.add(it.key(), modId);
        }
    } else {
        // Legacy v1: flat relPath -> modId, no fingerprints, unknown mode.
        rec.m_version = 1;
        rec.m_mode = -1;
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            rec.add(it.key(), it.value().toString());
    }
    return rec;
}

} // namespace solero
