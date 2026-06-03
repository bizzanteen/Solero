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
    QStringList profileNames() const;
    Profile* loadProfile(const QString& name);
    Profile* activeProfile() { return m_active.get(); }

private:
    QString m_root;
    std::unique_ptr<Profile> m_active;
};

} // namespace solero
