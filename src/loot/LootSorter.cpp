#include "LootSorter.h"
#include "core/AppConfig.h"
#include "install/PluginScanner.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <vector>
#include <string>
#include <filesystem>

#include <loot/api.h>
#include <loot/enum/game_type.h>

namespace solero {

QString LootSorter::masterlistDir() {
    return AppConfig::dataRoot() + "/loot";
}

QString LootSorter::masterlistPath() {
    return masterlistDir() + "/masterlist.yaml";
}

LootSorter::SortResult LootSorter::sort(PluginList& pluginList,
                                        const QString& gameDir,
                                        const QString& userlistPath) {
    SortResult result;
    try {
        // libloot needs the game's local appdata folder (Plugins.txt / loadorder.txt)
        // which on a Proton install lives inside the Wine prefix.
        QString localPath = AppConfig::instance().localAppDataDir();
        auto handle = loot::CreateGameHandle(
            loot::GameType::tes5se,
            std::filesystem::path(gameDir.toStdString()),
            std::filesystem::path(localPath.toStdString()));

        // Load metadata: masterlist (if present) then userlist (if present).
        auto& db = handle->GetDatabase();
        QString mlPath = masterlistPath();
        if (QFile::exists(mlPath))
            db.LoadMasterlist(std::filesystem::path(mlPath.toStdString()));
        if (!userlistPath.isEmpty() && QFile::exists(userlistPath))
            db.LoadUserlist(std::filesystem::path(userlistPath.toStdString()));

        // Best-effort: load the current load order state so SortPlugins can
        // account for active/inactive plugins. Non-fatal if it fails.
        try { handle->LoadCurrentLoadOrderState(); } catch (...) {}

        // Build the plugin path + name lists from our PluginList, using only
        // plugins that actually exist on disk in Data/.
        QString dataDir = gameDir + "/Data";
        std::vector<std::filesystem::path> pluginPaths;
        std::vector<std::string> pluginNames;
        for (int i = 0; i < pluginList.count(); ++i) {
            const auto& p = pluginList.at(i);
            QString full = dataDir + "/" + p.filename;
            if (!QFile::exists(full)) continue;
            pluginPaths.push_back(std::filesystem::path(full.toStdString()));
            pluginNames.push_back(p.filename.toStdString());
        }

        if (pluginNames.empty()) {
            result.success = true; // nothing to sort
            return result;
        }

        handle->LoadPlugins(pluginPaths, true /* headers only */);
        auto sorted = handle->SortPlugins(pluginNames);

        // Rebuild PluginList in sorted order, preserving enabled state.
        PluginList newList;
        for (const auto& name : sorted) {
            QString fn = QString::fromStdString(name);
            if (auto* existing = pluginList.findByFilename(fn)) {
                newList.append(*existing);
            } else {
                // Synthesized entry for a plugin LOOT returned that wasn't in our
                // list. There's no prior state to preserve (findByFilename failed),
                // so default to active only if the file actually exists on disk;
                // classify via TES4 header flags, falling back to the extension.
                PluginEntry pe;
                pe.filename = fn;
                pe.enabled  = QFile::exists(dataDir + "/" + fn);
                PluginFlags pf = PluginScanner::readFlags(dataDir + "/" + fn);
                if (pf.ok) {
                    pe.isMaster = pf.isMaster;
                    pe.isLight  = pf.isLight;
                } else {
                    pe.isMaster = fn.endsWith(".esm", Qt::CaseInsensitive);
                    pe.isLight  = fn.endsWith(".esl", Qt::CaseInsensitive);
                }
                newList.append(pe);
            }
        }
        // Append any plugins that weren't sorted (e.g. missing on disk) so we
        // don't silently drop them.
        for (int i = 0; i < pluginList.count(); ++i) {
            const auto& p = pluginList.at(i);
            if (!newList.findByFilename(p.filename))
                newList.append(p);
        }

        // Preserve manual load-order control state (lock + pins) across the
        // wholesale list rebuild; the caller re-applies pins after sorting.
        newList.copyOrderState(pluginList);
        pluginList = newList;
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = QString::fromStdString(e.what());
    } catch (...) {
        result.success = false;
        result.errorMessage = "unknown LOOT error";
    }
    return result;
}

bool LootSorter::updateMasterlist(const QString& mlPath) {
    QDir().mkpath(QFileInfo(mlPath).path());
    // LOOT Skyrim SE masterlist. The v0.26 branch is metadata-syntax compatible
    // with libloot 0.27+. Best-effort via curl; failure is non-fatal.
    QString cmd = QString(
        "curl -fsSL -o '%1' "
        "https://raw.githubusercontent.com/loot/skyrimse/v0.26/masterlist.yaml")
        .arg(mlPath);
    return system(cmd.toLocal8Bit().constData()) == 0;
}

} // namespace solero
