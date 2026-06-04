#include "DependencyChecker.h"
#include "core/ModList.h"
#include "core/Types.h"
#include <QDir>
#include <QFileInfo>

namespace solero {

// Resolve a child entry of `parent` case-insensitively (e.g. Data vs data).
// Returns the actual path or empty.
static QString childCI(const QString& parent, const QString& name) {
    QDir d(parent);
    if (!d.exists()) return {};
    for (const QString& e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
        if (e.compare(name, Qt::CaseInsensitive) == 0)
            return parent + "/" + e;
    return {};
}

// Resolve <modDir>/Data/SKSE/Plugins case-insensitively (one listing per level).
static QString sksePluginsDir(const QString& modDir) {
    QString data = childCI(modDir, "Data");
    if (data.isEmpty()) return {};
    QString skse = childCI(data, "SKSE");
    if (skse.isEmpty()) return {};
    return childCI(skse, "Plugins");
}

// Shallow checks (no full-subtree recursion - critical for large symlinked imports).
static bool dirHasSksePluginDll(const QString& modDir) {
    QString pl = sksePluginsDir(modDir);
    if (pl.isEmpty()) return false;
    return !QDir(pl).entryList({"*.dll", "*.DLL"}, QDir::Files).isEmpty();
}
static bool dirHasSkseLoader(const QString& modDir) {
    // SKSE loader/runtime sit at the mod's game-root.
    if (!childCI(modDir, "skse64_loader.exe").isEmpty()) return true;
    return !QDir(modDir).entryList({"skse64_*.dll"}, QDir::Files).isEmpty();
}
static bool dirHasAddressLib(const QString& modDir) {
    QString pl = sksePluginsDir(modDir);
    if (pl.isEmpty()) return false;
    return !QDir(pl).entryList({"versionlib-*.bin"}, QDir::Files).isEmpty();
}

QHash<QString, QStringList> DependencyChecker::check(const ModList& list, const QString& stagingRoot) {
    bool haveSkse = false, haveAddrLib = false;
    for (const auto& e : list) {
        if (e.type != EntryType::Mod || !e.enabled) continue;
        QString dir = stagingRoot + "/" + e.id;
        if (!haveSkse && dirHasSkseLoader(dir)) haveSkse = true;
        if (!haveAddrLib && dirHasAddressLib(dir)) haveAddrLib = true;
        if (haveSkse && haveAddrLib) break;
    }

    QHash<QString, QStringList> result;
    for (const auto& e : list) {
        if (e.type != EntryType::Mod || !e.enabled) continue;
        QString dir = stagingRoot + "/" + e.id;
        if (!dirHasSksePluginDll(dir)) continue; // only SKSE-plugin mods carry these deps
        QStringList warns;
        if (!haveSkse)    warns << "Requires SKSE64 (not installed/enabled)";
        if (!haveAddrLib) warns << "Requires Address Library for SKSE Plugins (not installed/enabled)";
        if (!warns.isEmpty()) result.insert(e.id, warns);
    }
    return result;
}

} // namespace solero
