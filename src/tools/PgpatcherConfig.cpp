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

    // Mod manager = None (0). PGPatcher reads the deployed game Data directly -
    // which Solero has ALREADY conflict-resolved at deploy time (last-writer-wins
    // in load order), so the merged Data is the correct final state to patch.
    // MO2 mode (2) would give PGPatcher per-mod conflict detection, and Solero CAN
    // build a real populated fake-MO2 instance for it - but PGPatcher's MO2 "Set
    // Mods" conflict UI renders black/garbled under Proton on this hardware (RADV;
    // not fixable via wined3d), making it unusable. So we default to None. The
    // populate-instance code (RadiumPrep::writeFakeMo2 populateMods) is kept but
    // not invoked for PGPatcher; mo2instancedir is still written (harmless, ignored
    // in None mode) for if MO2 mode is ever revisited.
    QJsonObject mm = params.value("modmanager").toObject();
    mm["type"] = 0; // None - read the deployed (already-merged) game Data
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
