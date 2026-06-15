#include "ProfileManager.h"
#include <QDir>

namespace solero {

ProfileManager::ProfileManager(const QString& root) : m_root(root) {
    QDir().mkpath(root);
}

bool ProfileManager::createProfile(const QString& name, const QString& cloneFrom) {
    QString path = m_root + "/" + name;
    if (QDir(path).exists()) return false;
    QDir().mkpath(path);
    if (!cloneFrom.isEmpty()) {
        Profile src(cloneFrom, m_root);
        src.load();
        Profile dst(name, m_root);
        dst.modList()     = src.modList();
        dst.pluginList()  = src.pluginList();
        dst.executables() = src.executables();
        return dst.save();
    }
    Profile p(name, m_root);
    return p.save();
}

bool ProfileManager::deleteProfile(const QString& name) {
    return QDir(m_root + "/" + name).removeRecursively();
}

bool ProfileManager::renameProfile(const QString& oldName, const QString& newName) {
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) return false;
    if (trimmed.contains('/') || trimmed.contains('\\')) return false;
    if (trimmed == oldName) return false;
    const QString src = m_root + "/" + oldName;
    const QString dst = m_root + "/" + trimmed;
    if (!QDir(src).exists()) return false;
    if (QDir(dst).exists()) return false;
    return QDir().rename(src, dst);
}

QStringList ProfileManager::profileNames() const {
    return QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}

Profile* ProfileManager::loadProfile(const QString& name) {
    m_active = std::make_unique<Profile>(name, m_root);
    m_active->load();
    return m_active.get();
}

} // namespace solero
