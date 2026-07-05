#include "ProfileManager.h"
#include "core/Log.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace solero {

namespace {
// Recursively copy the CONTENTS of srcDir into dstDir (which must already exist).
// Returns false if any file or subdirectory copy fails; a warning is logged per
// failed file. Hidden entries (e.g. .bak files) are included so the copy is faithful.
bool copyDirContents(const QString& srcDir, const QString& dstDir) {
    bool ok = true;
    const auto entries = QDir(srcDir).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo& fi : entries) {
        const QString target = dstDir + "/" + fi.fileName();
        if (fi.isDir()) {
            if (!QDir().mkpath(target) || !copyDirContents(fi.absoluteFilePath(), target))
                ok = false;
        } else {
            QFile::remove(target); // in case the fresh profile dir already holds it
            if (!QFile::copy(fi.absoluteFilePath(), target)) {
                qCWarning(lcProfile) << "createProfile: failed to copy"
                                     << fi.absoluteFilePath() << "->" << target;
                ok = false;
            }
        }
    }
    return ok;
}
} // namespace

ProfileManager::ProfileManager(const QString& root) : m_root(root) {
    QDir().mkpath(root);
}

bool ProfileManager::createProfile(const QString& name, const QString& cloneFrom) {
    QString path = m_root + "/" + name;
    if (QDir(path).exists()) {
        qCWarning(lcProfile) << "createProfile:" << name << "already exists at" << path;
        return false;
    }
    QDir().mkpath(path);
    if (!cloneFrom.isEmpty()) {
        const QString srcPath = m_root + "/" + cloneFrom;
        if (!QDir(srcPath).exists()) {
            qCWarning(lcProfile) << "createProfile:" << name << "clone source" << cloneFrom
                                 << "does not exist at" << srcPath;
            return false;
        }
        qCInfo(lcProfile) << "creating profile" << name << "cloned from" << cloneFrom
                          << "(full directory copy)";
        // Filesystem-copy the whole profile dir so the clone reproduces EVERYTHING:
        // modlist, plugins, executables, file-rules (hidden files + winner overrides),
        // load-order state (pins + lock), shader cache, and the three INIs - not just
        // the three lists the old field-by-field copy handled. Each file is a fresh
        // copy, so the cloned profile is fully independent of its source.
        const bool ok = copyDirContents(srcPath, path);
        if (!ok) qCWarning(lcProfile) << "createProfile:" << name << "clone copy had failures";
        return ok;
    }
    qCInfo(lcProfile) << "creating profile" << name;
    Profile p(name, m_root);
    const bool ok = p.save();
    if (!ok) qCWarning(lcProfile) << "createProfile:" << name << "save failed";
    return ok;
}

bool ProfileManager::deleteProfile(const QString& name) {
    qCInfo(lcProfile) << "deleting profile" << name;
    const bool ok = QDir(m_root + "/" + name).removeRecursively();
    if (!ok) qCWarning(lcProfile) << "deleteProfile:" << name << "removeRecursively failed";
    return ok;
}

bool ProfileManager::renameProfile(const QString& oldName, const QString& newName) {
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) return false;
    if (trimmed.contains('/') || trimmed.contains('\\')) return false;
    if (trimmed == oldName) return false;
    const QString src = m_root + "/" + oldName;
    const QString dst = m_root + "/" + trimmed;
    if (!QDir(src).exists()) return false;
    if (QDir(dst).exists()) {
        qCWarning(lcProfile) << "renameProfile:" << oldName << "-> destination" << trimmed << "already exists";
        return false;
    }
    const bool ok = QDir().rename(src, dst);
    if (ok) qCInfo(lcProfile) << "renamed profile" << oldName << "->" << trimmed;
    else    qCWarning(lcProfile) << "renameProfile:" << oldName << "->" << trimmed << "rename failed";
    return ok;
}

QStringList ProfileManager::profileNames() const {
    return QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}

Profile* ProfileManager::loadProfile(const QString& name) {
    qCInfo(lcProfile) << "loading active profile" << name;
    m_active = std::make_unique<Profile>(name, m_root);
    m_active->load();
    return m_active.get();
}

} // namespace solero
