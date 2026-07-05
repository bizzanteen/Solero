#include "DependencyChecker.h"
#include "core/ModList.h"
#include "core/Types.h"
#include "core/StagingFolder.h"
#include <QDir>
#include <QFileInfo>
#include <QDateTime>

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

// Per-mod cache of the three dependency signals, keyed by mod id and validated
// against the staging dir + its mtime so an unchanged mod isn't re-walked on the
// next health refresh (called on every plugin toggle / rename / variant switch).
namespace {
struct ModScan { QString dir; qint64 mtime = -1; bool hasDll = false, hasLoader = false, hasAddrLib = false; };
QHash<QString, ModScan> g_scanCache;

const ModScan& scanMod(const QString& id, const QString& dir) {
    const qint64 mtime = QFileInfo(dir).lastModified().toMSecsSinceEpoch();
    auto it = g_scanCache.find(id);
    if (it != g_scanCache.end() && it->dir == dir && it->mtime == mtime)
        return it.value();
    ModScan s;
    s.dir = dir;
    s.mtime = mtime;
    s.hasDll = dirHasSksePluginDll(dir);
    s.hasLoader = dirHasSkseLoader(dir);
    s.hasAddrLib = dirHasAddressLib(dir);
    return *g_scanCache.insert(id, s);
}
} // namespace

void DependencyChecker::invalidate(const QString& modId) { g_scanCache.remove(modId); }
void DependencyChecker::invalidate() { g_scanCache.clear(); }

QHash<QString, QStringList> DependencyChecker::check(const ModList& list, const QString& stagingRoot) {
    // One shallow (cached) scan per enabled mod, then reason over the results.
    bool haveSkse = false, haveAddrLib = false;
    QStringList plugModIds; // enabled mods carrying an SKSE plugin dll
    for (const auto& e : list) {
        if (e.type != EntryType::Mod || !e.enabled) continue;
        const ModScan& s = scanMod(e.id, stagingPathFor(stagingRoot, e));
        haveSkse    = haveSkse    || s.hasLoader;
        haveAddrLib = haveAddrLib || s.hasAddrLib;
        if (s.hasDll) plugModIds << e.id;
    }

    QHash<QString, QStringList> result;
    if (haveSkse && haveAddrLib) return result; // deps satisfied - nothing to warn
    for (const auto& id : plugModIds) {
        QStringList warns;
        if (!haveSkse)    warns << "Requires SKSE64 (not installed/enabled)";
        if (!haveAddrLib) warns << "Requires Address Library for SKSE Plugins (not installed/enabled)";
        if (!warns.isEmpty()) result.insert(id, warns);
    }
    return result;
}

} // namespace solero
