#include "DependencyChecker.h"
#include "core/ModList.h"
#include "core/Types.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace solero {

// True if the enabled mod's staging dir contains a file matching any predicate signal.
static bool dirHasSksePluginDll(const QString& modDir) {
    QDirIterator it(modDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString f = it.next();
        QString lower = f.toLower();
        if (lower.contains("/data/skse/plugins/") && lower.endsWith(".dll")) return true;
    }
    return false;
}
static bool dirHasSkseLoader(const QString& modDir) {
    QDirIterator it(modDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString name = QFileInfo(it.next()).fileName().toLower();
        if (name == "skse64_loader.exe" || name.startsWith("skse64_") && name.endsWith(".dll")) return true;
    }
    return false;
}
static bool dirHasAddressLib(const QString& modDir) {
    QDirIterator it(modDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString name = QFileInfo(it.next()).fileName().toLower();
        if (name.startsWith("versionlib-") && name.endsWith(".bin")) return true;
    }
    return false;
}

QHash<QString, QStringList> DependencyChecker::check(const ModList& list, const QString& stagingRoot) {
    // First, does any enabled mod provide SKSE / Address Library?
    bool haveSkse = false, haveAddrLib = false;
    for (const auto& e : list) {
        if (e.type != EntryType::Mod || !e.enabled) continue;
        QString dir = stagingRoot + "/" + e.id;
        if (dirHasSkseLoader(dir)) haveSkse = true;
        if (dirHasAddressLib(dir)) haveAddrLib = true;
    }

    QHash<QString, QStringList> result;
    for (const auto& e : list) {
        if (e.type != EntryType::Mod || !e.enabled) continue;
        QString dir = stagingRoot + "/" + e.id;
        if (!dirHasSksePluginDll(dir)) continue;        // only SKSE-plugin mods have these deps
        if (dirHasSkseLoader(dir) || dirHasAddressLib(dir)) {
            // This mod IS skse/addrlib itself - skip self.
        }
        QStringList warns;
        if (!haveSkse)    warns << "Requires SKSE64 (not installed/enabled)";
        if (!haveAddrLib) warns << "Requires Address Library for SKSE Plugins (not installed/enabled)";
        if (!warns.isEmpty()) result.insert(e.id, warns);
    }
    return result;
}

} // namespace solero
