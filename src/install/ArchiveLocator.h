#pragma once
#include <QString>
#include <QStringList>
#include <QHash>

namespace solero {

struct ModEntry;
class Profile;

// Finds the source archive a staged mod was installed from, searching the Solero
// downloads dir and every imported MO2/Wabbajack instance's download_directory.
// Tries stored sourceArchive, then meta.ini installationFile, then a modID/fileID
// sidecar index, then a fuzzy basename match. Indexes are lazy and cached per locator.
class ArchiveLocator {
public:
    explicit ArchiveLocator(const QString& stagingRoot);

    // Absolute path to the located archive, or empty if none found.
    QString locate(const ModEntry& mod);

    // Discover the download_directory of every imported instance referenced by a
    // profile's staged mods (resolve a staged symlink -> walk up to ModOrganizer
    // .ini -> read download_directory; Z:\ paths mapped to the filesystem root).
    // Public so a UI/test can pre-seed; locate() also discovers per-mod on demand.
    QStringList discoverInstanceDownloadDirs(const Profile& profile);

    // pure-ish helpers (no profile state), exposed for reuse/testing
    // Map an MO2 ModOrganizer.ini download_directory value to a filesystem path.
    // Handles @ByteArray()/quote wrapping and Windows drive letters (Z:\… -> the
    // path under '/'); falls back to a sibling Downloads dir of instanceRoot.
    static QString mapDownloadDir(QString raw, const QString& instanceRoot);

private:
    QString m_stagingRoot;

    // Lazily-discovered set of download directories (Solero downloads + instances).
    QStringList m_downloadDirs;
    bool m_dirsScanned = false;
    void ensureDownloadDirs();
    void addDir(const QString& dir);

    // (modID|fileID) and (modID|) -> archive path, from sidecars across all dirs.
    QHash<QString, QString> m_modFileIndex;
    // Every archive basename -> absolute path, for fuzzy matching.
    QList<QPair<QString, QString>> m_allArchives; // {basename, absPath}
    bool m_indexBuilt = false;
    void buildIndex();

    // Resolve a staged mod's origin source folder (the dir holding meta.ini that a
    // staged symlink points into). Returns empty for natively-installed mods.
    QString resolveSourceFolder(const ModEntry& mod) const;
};

} // namespace solero
