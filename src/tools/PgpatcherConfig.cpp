#include "PgpatcherConfig.h"
#include <QJsonObject>

namespace solero {
namespace PgpatcherConfig {

QString winePath(const QString& native) {
    QString w = native;
    // Each '/' becomes a single backslash in the VALUE; QJsonDocument then
    // JSON-escapes it to "\\" on disk (so "Z:\\home\\..." in the file).
    w.replace('/', QStringLiteral("\\"));
    return "Z:" + w;
}

QJsonObject buildSettings(const QJsonObject& existing,
                          const QString& gameDir,
                          const QString& fakeMo2Dir,
                          const QString& outputDir) {
    QJsonObject root = existing;
    QJsonObject params = root.value("params").toObject();

    QJsonObject game = params.value("game").toObject();
    game["dir"] = winePath(gameDir);
    game["type"] = 0; // Skyrim SE
    params["game"] = game;

    // Mod manager = Mod Organizer 2 (2). PGPatcher needs the per-mod boundaries to
    // DETECT CONFLICTS between mods - which None mode (reading the merged game Data)
    // cannot do. We point it at a fake-MO2 instance that Solero populates from the
    // active profile (mods/ symlinks + a matching modlist.txt; see
    // RadiumPrep::writeFakeMo2 populateMods), so MO2 mode sees the real, ordered set
    // of mods and reports inter-mod conflicts. mo2useloosefileorder keeps loose-file
    // priority = modlist order.
    QJsonObject mm = params.value("modmanager").toObject();
    mm["type"] = 2; // Mod Organizer 2 - populated fake-MO2 instance enables conflict detection
    mm["mo2instancedir"] = winePath(fakeMo2Dir);
    mm["mo2useloosefileorder"] = true;
    params["modmanager"] = mm;

    QJsonObject output = params.value("output").toObject();
    output["dir"] = winePath(outputDir);
    if (!output.contains("pluginlang")) output["pluginlang"] = "English";
    if (!output.contains("zip"))        output["zip"] = false;
    params["output"] = output;

    root["params"] = params;
    return root;
}

} // namespace PgpatcherConfig
} // namespace solero
