#pragma once
#include "Profile.h"
#include <QStringList>
#include <memory>

namespace solero {

class ProfileManager {
public:
    explicit ProfileManager(const QString& profilesRoot);

    bool createProfile(const QString& name, const QString& cloneFrom = {});
    bool deleteProfile(const QString& name);
    // Rename a profile directory. Returns false if newName is empty/contains path
    // separators, the source doesn't exist, or the target already exists. Does not
    // touch the active profile pointer or any name-keyed state outside the profiles
    // root - the caller (MainWindow) handles overwrite-dir / lastProfile / reload.
    bool renameProfile(const QString& oldName, const QString& newName);
    QStringList profileNames() const;
    Profile* loadProfile(const QString& name);
    Profile* activeProfile() { return m_active.get(); }
    // The profiles root, so a caller can construct a Profile for a name that isn't
    // the active one (e.g. to set flags on a just-created profile).
    const QString& root() const { return m_root; }

private:
    QString m_root;
    std::unique_ptr<Profile> m_active;
};

} // namespace solero
