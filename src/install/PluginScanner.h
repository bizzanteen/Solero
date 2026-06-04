#pragma once
#include <QString>
#include <QStringList>
namespace solero { class ModList; }
namespace solero {
class PluginScanner {
public:
    // Ordered, de-duplicated plugin filenames provided by enabled mods
    // (scanning each mod's Data/ top level for *.esp/*.esm/*.esl), in mod order.
    static QStringList scan(const ModList& list, const QString& stagingRoot);

    // All plugin filenames physically present in the game's Data/ folder
    // (top level *.esp/*.esm/*.esl), masters first then the rest, alphabetical.
    static QStringList scanGameData(const QString& gameDir);
};
}
