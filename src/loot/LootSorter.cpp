#include "LootSorter.h"
#include "core/AppConfig.h"
#include "core/Log.h"
#include "install/PluginScanner.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <vector>
#include <string>
#include <optional>
#include <filesystem>

#include <loot/api.h>
#include <loot/enum/game_type.h>

namespace solero {

namespace {
// Last sort's dirty-plugin flags (lowercased filename -> reason). File-static so
// the plugin list can read it after a sort without MainWindow having to relay it.
QHash<QString, QString> g_dirtyPlugins;

// A cleaning utility name from the masterlist is often a CommonMark link like
// "[SSEEdit](https://...)"; show just the label.
QString cleaningUtilityLabel(const std::string& utility) {
    QString u = QString::fromStdString(utility).trimmed();
    const int lb = u.indexOf(QLatin1Char('['));
    const int rb = u.indexOf(QLatin1Char(']'), lb + 1);
    if (lb >= 0 && rb > lb) return u.mid(lb + 1, rb - lb - 1);
    return u.isEmpty() ? QStringLiteral("SSEEdit") : u;
}

// "N ITM, M UDR, K deleted navmeshes - clean with <utility>" for one dirty
// record (uses a plain hyphen so the string is 7-bit clean; the UI adds glyphs).
QString dirtyReason(const loot::PluginCleaningData& d) {
    QStringList parts;
    if (d.GetITMCount() > 0)             parts << QStringLiteral("%1 ITM").arg(d.GetITMCount());
    if (d.GetDeletedReferenceCount() > 0) parts << QStringLiteral("%1 UDR").arg(d.GetDeletedReferenceCount());
    if (d.GetDeletedNavmeshCount() > 0)  parts << QStringLiteral("%1 deleted navmeshes").arg(d.GetDeletedNavmeshCount());
    const QString head = parts.isEmpty() ? QStringLiteral("needs cleaning") : parts.join(QStringLiteral(", "));
    return head + QStringLiteral(" - clean with ") + cleaningUtilityLabel(d.GetCleaningUtility());
}
} // namespace

const QHash<QString, QString>& LootSorter::dirtyPlugins() { return g_dirtyPlugins; }

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
    qCInfo(lcLoot) << "LOOT sort start:" << pluginList.count() << "plugins";
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
            qCInfo(lcLoot) << "LOOT sort done: nothing to sort (no plugins on disk)";
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

        // after loading the plugins, ask LOOT which are known-dirty
        // (ITM/UDR/navmesh records needing SSEEdit). Best-effort and additive -
        // guarded so a libloot without cleaning data (or a metadata hiccup) just
        // yields no flags rather than failing the sort. Plugins are loaded
        // headers-only, so their CRC is unavailable; when it is available we
        // report only the dirty record matching the installed version.
        try {
            for (const auto& name : sorted) {
                auto meta = db.GetPluginMetadata(name, true /* incl. user */, false /* no cond. eval */);
                if (!meta) continue;
                const auto dirty = meta->GetDirtyInfo();
                if (dirty.empty()) continue;
                std::optional<uint32_t> crc;
                try { if (auto pi = handle->GetPlugin(name)) crc = pi->GetCRC(); } catch (...) {}
                for (const auto& d : dirty) {
                    if (crc && d.GetCRC() != 0 && d.GetCRC() != *crc) continue;
                    result.dirtyPlugins.insert(QString::fromStdString(name).toLower(), dirtyReason(d));
                    break; // one flag per plugin
                }
            }
        } catch (...) {
            qCInfo(lcLoot) << "LOOT cleaning-data query unavailable; skipping dirty flags";
        }
        g_dirtyPlugins = result.dirtyPlugins;
        qCInfo(lcLoot) << "LOOT sort done:" << int(sorted.size()) << "plugins sorted,"
                       << result.dirtyPlugins.size() << "dirty";
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = QString::fromStdString(e.what());
        qCWarning(lcLoot) << "LOOT sort failed:" << result.errorMessage;
    } catch (...) {
        result.success = false;
        result.errorMessage = "An unexpected LOOT error occurred. Try deploying first, then sort again.";
        qCWarning(lcLoot) << "LOOT sort failed: unknown (non-std::exception) error";
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
    int rc = system(cmd.toLocal8Bit().constData());
    if (rc != 0)
        qCWarning(lcLoot) << "masterlist update failed: curl exited" << rc;
    return rc == 0;
}

} // namespace solero
