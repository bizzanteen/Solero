#pragma once
#include "core/PluginList.h"
#include <QString>
#include <QStringList>

namespace solero {

class LootSorter {
public:
    struct SortResult {
        bool success = false;
        QString errorMessage;
        QStringList warnings;
    };

    // Sort pluginList in place using LOOT (masterlist + optional userlist).
    // gameDir: the Skyrim SE install directory (plugins live in gameDir/Data).
    // userlistPath: path to the profile's loot-userlist.yaml (may not exist).
    static SortResult sort(PluginList& pluginList,
                           const QString& gameDir,
                           const QString& userlistPath);

    // Best-effort download of the Skyrim SE masterlist to masterlistPath.
    static bool updateMasterlist(const QString& masterlistPath);

    static QString masterlistDir();  // ~/.local/share/solero/loot
    static QString masterlistPath(); // masterlistDir()/masterlist.yaml
};

} // namespace solero
