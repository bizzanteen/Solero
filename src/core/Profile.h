#pragma once
#include "ModList.h"
#include "PluginList.h"
#include "Types.h"
#include <QString>
#include <QList>
#include <QHash>
#include <QSet>

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
    QString fileRulesPath()     const;

    // Per-file conflict resolution (MO2 ".mohidden" + Vortex per-file winner).
    // relPath is in the same form DeployEngine uses: path relative to the mod
    // root (e.g. "Data/SKSE/Plugins/foo.dll").

    // A) Hidden files: a file hidden within a mod is skipped on deploy, letting
    //    the next-priority provider win.
    bool isFileHidden(const QString& modId, const QString& relPath) const;
    void setFileHidden(const QString& modId, const QString& relPath, bool hidden);
    const QHash<QString, QSet<QString>>& hiddenFiles() const { return m_hiddenFiles; }

    // B) Winner overrides: force a chosen mod to provide a path on deploy,
    //    regardless of load-order priority. Empty modId == no override.
    QString winnerOverride(const QString& relPath) const;
    void setWinnerOverride(const QString& relPath, const QString& modId);
    void clearWinnerOverride(const QString& relPath);
    const QHash<QString, QString>& fileOverrides() const { return m_fileOverrides; }

    bool save() const;
    bool load();

private:
    QString m_name;
    QString m_path;
    ModList m_modList;
    PluginList m_pluginList;
    QList<Executable> m_executables;
    QHash<QString, QSet<QString>> m_hiddenFiles;  // modId  -> hidden relPaths
    QHash<QString, QString>       m_fileOverrides; // relPath -> forced winner modId

    bool saveExecutables() const;
    bool loadExecutables();
    bool saveFileRules() const;
    bool loadFileRules();
};

} // namespace solero
