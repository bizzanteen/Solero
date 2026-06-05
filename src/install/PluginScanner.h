#pragma once
#include <QString>
#include <QStringList>
namespace solero { class ModList; }
namespace solero {
struct PluginFlags { bool isMaster = false; bool isLight = false; bool ok = false; };

class PluginScanner {
public:
    // Ordered, de-duplicated plugin filenames provided by enabled mods
    // (scanning each mod's Data/ top level for *.esp/*.esm/*.esl), in mod order.
    static QStringList scan(const ModList& list, const QString& stagingRoot);

    // All plugin filenames physically present in the game's Data/ folder
    // (top level *.esp/*.esm/*.esl), ordered as a proper Skyrim load-order
    // baseline: base-game masters first, then other masters, then ESL/light,
    // then regular plugins.
    static QStringList scanGameData(const QString& gameDir);

    // Read a plugin's TES4 record header flags from disk. ok=true only when the
    // file begins with a valid "TES4" signature; callers fall back to the file
    // extension when ok=false.
    static PluginFlags readFlags(const QString& pluginPath);

    // Read the master files (dependencies) a plugin declares in its TES4 header
    // (one entry per MAST subrecord, in order). Empty list on any failure.
    static QStringList readMasters(const QString& pluginPath);

    // The canonical locked set of official plugins in official load order:
    // the 5 base masters, then every line of <gameDir>/Skyrim.ccc (Bethesda's
    // Creation Club / Anniversary order). Just the 5 base masters if no .ccc.
    static QStringList officialPlugins(const QString& gameDir);
};
}
