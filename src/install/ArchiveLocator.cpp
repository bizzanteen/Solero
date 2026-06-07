#include "ArchiveLocator.h"
#include "core/Types.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace solero {

namespace {

// Lowercase + strip every non-alphanumeric char (fuzzy archive-name matching).
QString normName(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) if (c.isLetterOrNumber()) out += c.toLower();
    return out;
}

// Case-insensitive child lookup under `parent`; returns the real-cased full path.
QString childCI(const QString& parent, const QString& name) {
    if (parent.isEmpty() || name.isEmpty()) return {};
    QDir d(parent);
    if (!d.exists()) return {};
    for (const QString& e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
        if (e.compare(name, Qt::CaseInsensitive) == 0) return parent + "/" + e;
    return {};
}

// Walk up from `dir` until a dir containing `marker` is found (capped depth).
QString findAncestorWith(QString dir, const QString& marker, int maxUp = 10) {
    for (int i = 0; i < maxUp && !dir.isEmpty(); ++i) {
        if (QFileInfo::exists(dir + "/" + marker)) return dir;
        QDir d(dir);
        if (!d.cdUp()) break;
        dir = d.absolutePath();
    }
    return {};
}

QString unwrapIni(QString v) {
    v = v.trimmed();
    if (v.startsWith("@ByteArray(", Qt::CaseInsensitive)) {
        int o = v.indexOf('('), c = v.lastIndexOf(')');
        if (o >= 0 && c > o) v = v.mid(o + 1, c - o - 1);
    }
    v.remove('"');
    return v.trimmed();
}

const QStringList kArchiveExts = {"*.zip", "*.7z", "*.rar", "*.tar", "*.gz", "*.001"};

} // namespace

ArchiveLocator::ArchiveLocator(const QString& stagingRoot) : m_stagingRoot(stagingRoot) {}

void ArchiveLocator::addDir(const QString& dir) {
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) return;
    const QString canon = QDir(dir).canonicalPath();
    const QString use = canon.isEmpty() ? dir : canon;
    if (!m_downloadDirs.contains(use)) {
        m_downloadDirs.append(use);
        m_indexBuilt = false; // a new dir invalidates the cached index
    }
}

void ArchiveLocator::ensureDownloadDirs() {
    if (!m_dirsScanned) {
        m_dirsScanned = true;
        addDir(AppConfig::instance().downloadsDir());
    }
}

QString ArchiveLocator::resolveSourceFolder(const ModEntry& mod) const {
    const QString modDir = m_stagingRoot + "/" + mod.id;
    if (!QFileInfo(modDir).isDir()) return {};
    // Find the first staged file that is a symlink; its target points into the
    // origin mod folder (MO2/Wabbajack instance mods/<Name>/…).
    QDirIterator it(modDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString full = it.next();
        QFileInfo fi(full);
        if (!fi.isSymLink()) continue;
        const QString target = fi.symLinkTarget();
        if (target.isEmpty()) continue;
        // Walk up from the target's directory to the dir that holds meta.ini.
        return findAncestorWith(QFileInfo(target).absolutePath(), "meta.ini");
    }
    return {};
}

QStringList ArchiveLocator::discoverInstanceDownloadDirs(const Profile& profile) {
    QStringList out;
    QStringList seenInstances;
    for (const auto& m : profile.modList()) {
        if (m.type != EntryType::Mod) continue;
        const QString srcFolder = resolveSourceFolder(m);
        if (srcFolder.isEmpty()) continue;
        const QString instRoot = findAncestorWith(srcFolder, "ModOrganizer.ini");
        if (instRoot.isEmpty() || seenInstances.contains(instRoot)) continue;
        seenInstances.append(instRoot);

        QSettings ini(instRoot + "/ModOrganizer.ini", QSettings::IniFormat);
        QString raw = ini.value("Settings/download_directory").toString();
        if (raw.isEmpty()) raw = ini.value("download_directory").toString();
        const QString dl = mapDownloadDir(unwrapIni(raw), instRoot);
        if (!dl.isEmpty() && !out.contains(dl)) { out.append(dl); addDir(dl); }
    }
    return out;
}

QString ArchiveLocator::mapDownloadDir(QString raw, const QString& instanceRoot) {
    raw = unwrapIni(raw);
    raw.replace('\\', '/');

    auto asDir = [](const QString& p) -> QString {
        return (!p.isEmpty() && QFileInfo(p).isDir()) ? p : QString();
    };

    if (!raw.isEmpty()) {
        static const QRegularExpression drive("^[A-Za-z]:/");
        if (drive.match(raw).hasMatch()) {
            const QString rest = raw.mid(2); // strip "X:", keeps the leading '/'
            if (QString r = asDir(rest); !r.isEmpty()) return r;          // Z: -> root
            if (QString r = asDir(QDir::homePath() + rest); !r.isEmpty()) return r;
        } else if (raw.startsWith('/')) {
            if (QString r = asDir(raw); !r.isEmpty()) return r;
        } else if (!instanceRoot.isEmpty()) {
            if (QString r = asDir(instanceRoot + "/" + raw); !r.isEmpty()) return r;
        }
    }
    // Fallback: a sibling downloads dir of the instance.
    if (!instanceRoot.isEmpty()) {
        for (const QString& n : {"downloads", "Downloads"}) {
            const QString c = instanceRoot + "/" + n;
            if (QFileInfo(c).isDir()) return c;
        }
    }
    return {};
}

void ArchiveLocator::buildIndex() {
    if (m_indexBuilt) return;
    ensureDownloadDirs();
    m_modFileIndex.clear();
    m_allArchives.clear();

    for (const QString& dir : m_downloadDirs) {
        const QFileInfoList files = QDir(dir).entryInfoList(kArchiveExts, QDir::Files, QDir::Time);
        for (const QFileInfo& fi : files) {
            const QString path = fi.absoluteFilePath();
            m_allArchives.append({ fi.completeBaseName(), path });

            QString modId, fileId;
            // MO2 ".meta" sidecar: [General] modID= fileID=
            const QString metaPath = path + ".meta";
            if (QFileInfo::exists(metaPath)) {
                QSettings s(metaPath, QSettings::IniFormat);
                modId  = s.value("modID",  s.value("modid")).toString().trimmed();
                fileId = s.value("fileID", s.value("fileid")).toString().trimmed();
            }
            // Solero nexus sidecar takes precedence when present.
            QFile sf(path + ".solero-nexus.json");
            if (sf.open(QIODevice::ReadOnly)) {
                const QJsonObject o = QJsonDocument::fromJson(sf.readAll()).object();
                const QString m = o.value("modId").toString().trimmed();
                const QString f = o.value("fileId").toString().trimmed();
                if (!m.isEmpty()) modId = m;
                if (!f.isEmpty()) fileId = f;
            }
            if (modId.isEmpty() || modId == "0") continue;
            const QString lm = modId.toLower(), lf = fileId.toLower();
            m_modFileIndex.insert(lm + "|" + lf, path);          // exact (modID,fileID)
            if (!m_modFileIndex.contains(lm + "|"))               // first-seen by modID
                m_modFileIndex.insert(lm + "|", path);
        }
    }
    m_indexBuilt = true;
}

QString ArchiveLocator::locate(const ModEntry& mod) {
    // (a) stored sourceArchive still on disk.
    if (!mod.sourceArchive.isEmpty() && QFileInfo::exists(mod.sourceArchive))
        return mod.sourceArchive;

    ensureDownloadDirs();

    // (b) origin source-folder meta.ini installationFile.
    const QString srcFolder = resolveSourceFolder(mod);
    if (!srcFolder.isEmpty()) {
        QSettings meta(srcFolder + "/meta.ini", QSettings::IniFormat);
        const QString instFile = unwrapIni(meta.value("installationFile").toString());

        QString instDl;
        const QString instRoot = findAncestorWith(srcFolder, "ModOrganizer.ini");
        if (!instRoot.isEmpty()) {
            QSettings ini(instRoot + "/ModOrganizer.ini", QSettings::IniFormat);
            QString raw = ini.value("Settings/download_directory").toString();
            if (raw.isEmpty()) raw = ini.value("download_directory").toString();
            instDl = mapDownloadDir(unwrapIni(raw), instRoot);
            if (!instDl.isEmpty()) addDir(instDl);
        }

        if (!instFile.isEmpty()) {
            const QString base = QFileInfo(instFile).fileName();
            for (const QString& d : { instDl, AppConfig::instance().downloadsDir() }) {
                const QString hit = childCI(d, base);
                if (!hit.isEmpty()) return hit;
            }
            if (QFileInfo::exists(instFile)) return instFile; // absolute installationFile
        }
    }

    // (c) (modID,fileID) -> archive index across all known download dirs.
    buildIndex();
    if (!mod.nexusModId.isEmpty()) {
        const QString lm = mod.nexusModId.toLower();
        const auto exact = m_modFileIndex.constFind(lm + "|" + mod.nexusFileId.toLower());
        if (exact != m_modFileIndex.constEnd()) return exact.value();
        const auto byMod = m_modFileIndex.constFind(lm + "|");
        if (byMod != m_modFileIndex.constEnd()) return byMod.value();
    }

    // (d) fuzzy name match against archive basenames.
    const QString target = normName(mod.name);
    if (target.size() >= 4) {
        for (const auto& pr : m_allArchives) {
            const QString cand = normName(pr.first);
            if (cand.isEmpty()) continue;
            if (cand.contains(target) || target.contains(cand)) return pr.second;
        }
    }
    return {};
}

} // namespace solero
