#pragma once
#include "ModList.h"
#include "PluginList.h"
#include "Types.h"
#include <QString>
#include <QList>

namespace solero {

class Profile {
public:
    explicit Profile(const QString& name, const QString& rootPath);

    const QString& name() const { return m_name; }
    const QString& path() const { return m_path; }

    ModList& modList() { return m_modList; }
    const ModList& modList() const { return m_modList; }

    PluginList& pluginList() { return m_pluginList; }
    const PluginList& pluginList() const { return m_pluginList; }

    QList<Executable>& executables() { return m_executables; }
    const QList<Executable>& executables() const { return m_executables; }

    QString modlistPath()       const;
    QString pluginsPath()       const;
    QString skyrimIniPath()     const;
    QString skyrimPrefsPath()   const;
    QString skyrimCustomPath()  const;
    QString executablesPath()   const;
    QString lootUserlistPath()  const;

    bool save() const;
    bool load();

private:
    QString m_name;
    QString m_path;
    ModList m_modList;
    PluginList m_pluginList;
    QList<Executable> m_executables;

    bool saveExecutables() const;
    bool loadExecutables();
};

} // namespace solero
