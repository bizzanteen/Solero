#pragma once
#include "core/PluginList.h"
#include <QString>
#include <QStringList>
#include <QHash>

namespace solero {

class LootSorter {
public:
    struct SortResult {
        bool success = false;
        QString errorMessage;
        QStringList warnings;
        // Plugins LOOT's metadata flags as dirty (needing ITM/UDR cleaning),
        // keyed by lowercased filename -> a short human reason. Empty
        // when the masterlist carries no cleaning data for the loaded plugins.
        QHash<QString, QString> dirtyPlugins;
    };

    // Sort pluginList in place using LOOT (masterlist + optional userlist).
    // gameDir: the Skyrim SE install directory (plugins live in gameDir/Data).
    // userlistPath: path to the profile's loot-userlist.yaml (may not exist).
    static SortResult sort(PluginList& pluginList,
                           const QString& gameDir,
                           const QString& userlistPath);

    // Dirty-plugin flags (lowercased filename -> short reason, e.g.
    // "12 ITM, 3 UDR - clean with SSEEdit") gathered during the most recent
    // sort(). Read by the plugin list to badge dirty rows without threading the
    // result through MainWindow; dirtiness is a property of the plugin file, so
    // the last sort's map stays valid across profile switches. Empty until the
    // first sort, or when libloot exposes no cleaning data.
    static const QHash<QString, QString>& dirtyPlugins();

    // Best-effort download of the Skyrim SE masterlist to masterlistPath.
    static bool updateMasterlist(const QString& masterlistPath);

    static QString masterlistDir();  // ~/.local/share/solero/loot
    static QString masterlistPath(); // masterlistDir()/masterlist.yaml
};

} // namespace solero
