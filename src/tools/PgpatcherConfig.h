#pragma once
#include <QString>

class QJsonObject;

namespace solero {
namespace PgpatcherConfig {

// Converts a native path (/home/...) to the Wine-style path PGPatcher/MO2 store
// in their JSON config: a "Z:" drive prefix with each '/' replaced by a single
// backslash. When QJsonDocument serializes the resulting QString, each backslash
// is JSON-escaped to "\\", so the bytes ON DISK read e.g. "Z:\\home\\eamon\\..."
// - matching how the launcher saves it (see the ASSOS settings.json).
QString winePath(const QString& native);

// Merge-builds PGPatcher's launcher config (cfg/settings.json). Starts from
// `existing` (preserving all keys the user/launcher already set - shaderpatcher,
// postpatcher, processing, blocklists, …) and overwrites only the three sections
// Solero owns so the launcher opens pre-populated:
//   params.game.dir        = winePath(gameDir),  params.game.type = 0
//   params.modmanager.type = 2 (MO2),
//   params.modmanager.mo2instancedir   = winePath(fakeMo2Dir),
//   params.modmanager.mo2useloosefileorder = true
//   params.output.dir      = winePath(outputDir) (the mod root, not /Data)
// params.output.pluginlang ("English") and params.output.zip (false) are filled
// in only if absent, so a user's prior choice is never clobbered.
QJsonObject buildSettings(const QJsonObject& existing,
                          const QString& gameDir,
                          const QString& fakeMo2Dir,
                          const QString& outputDir);

} // namespace PgpatcherConfig
} // namespace solero
