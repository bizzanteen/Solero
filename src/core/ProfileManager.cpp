#include "ProfileManager.h"
#include "core/Log.h"
#include <QDir>

namespace solero {

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
        qCInfo(lcProfile) << "creating profile" << name << "cloned from" << cloneFrom;
        Profile src(cloneFrom, m_root);
        src.load();
        Profile dst(name, m_root);
        dst.modList()     = src.modList();
        dst.pluginList()  = src.pluginList();
        dst.executables() = src.executables();
        const bool ok = dst.save();
        if (!ok) qCWarning(lcProfile) << "createProfile:" << name << "clone save failed";
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
