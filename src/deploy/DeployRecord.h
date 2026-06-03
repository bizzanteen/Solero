#pragma once
#include <QString>
#include <QHash>
#include <QStringList>

namespace solero {

class DeployRecord {
public:
    void add(const QString& relPath, const QString& modId);
    void remove(const QString& relPath);
    void clear();

    QString ownerOf(const QString& relPath) const;
    QStringList allPaths() const;
    int count() const { return m_files.size(); }

    bool saveToFile(const QString& path) const;
    static DeployRecord loadFromFile(const QString& path);

    static QString recordFilename() { return ".solero-deployed.json"; }

private:
    QHash<QString, QString> m_files; // relPath -> modId
};

} // namespace solero
