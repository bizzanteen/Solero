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

    // Mod manager = None (0). Solero is VFS-less: it hardlink-deploys the active
    // profile's mods (already conflict-resolved) into the game Data folder, so
    // PGPatcher reads the merged Data directly. MO2 mode (2) cannot work here -
    // Solero's fake-mo2 has an empty mods/ and modlist.txt, which makes PGPatcher
    // abort with "MO2 modlist.txt was empty, no mods found". mo2instancedir is
    // still written (harmless, ignored in None mode) so the launcher's field is
    // populated if the user ever switches modes manually.
    QJsonObject mm = params.value("modmanager").toObject();
    mm["type"] = 0; // None - read the deployed game Data directly
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
