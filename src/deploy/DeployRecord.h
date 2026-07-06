#pragma once
#include <QString>
#include <QHash>
#include <QStringList>

namespace solero {

class DeployRecord {
public:
    // Source-file fingerprint captured at link time. Lets an incremental
    // deploy tell whether a mod's staged content changed since it was deployed
    // (reinstall / variant switch / external tool edit) without re-linking. size < 0
    // means "no fingerprint recorded" (legacy v1 record, or a generated artifact
    // added via the two-arg add()).
    struct Fingerprint {
        qint64 size = -1;
        qint64 mtimeMs = -1;
        bool valid() const { return size >= 0; }
    };

    // Record a deployed path. The two-arg form carries no fingerprint (used for
    // generated artifacts like Plugins.txt that are rewritten every deploy).
    void add(const QString& relPath, const QString& modId);
    void add(const QString& relPath, const QString& modId, qint64 size, qint64 mtimeMs);
    void remove(const QString& relPath);
    void clear();

    QString ownerOf(const QString& relPath) const;
    Fingerprint fingerprintOf(const QString& relPath) const;
    bool contains(const QString& relPath) const;
    QStringList allPaths() const;
    int count() const { return m_files.size(); }

    // Record schema version: 2 for a fresh in-memory record or a v2 file; 1 for a
    // loaded legacy (flat relPath -> modId) file. deploy() forces a full redeploy
    // when a loaded record is < 2 (no fingerprints to diff against).
    int version() const { return m_version; }
    // DeployMode used for this deployment (see DeployMode.h). -1 = unknown/legacy.
    // A change vs the requested mode forces a full redeploy (every file's physical
    // form changes).
    int deployMode() const { return m_mode; }
    void setDeployMode(int mode) { m_mode = mode; }

    bool saveToFile(const QString& path) const;
    static DeployRecord loadFromFile(const QString& path);

    static QString recordFilename() { return ".solero-deployed.json"; }

private:
    struct Entry {
        QString modId;
        qint64 size = -1;
        qint64 mtimeMs = -1;
    };
    QHash<QString, Entry> m_files; // relPath -> {modId, fingerprint}
    int m_version = 2;   // in-memory records are current-format
    int m_mode = -1;     // deploy mode; -1 until set / loaded from a v2 file
};

} // namespace solero
