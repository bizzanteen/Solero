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
};
}
