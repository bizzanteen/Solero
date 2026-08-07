#pragma once
#include <QString>

class QJsonObject;

namespace solero {
namespace PgpatcherConfig {

// Native path -> the Wine-style path PGPatcher stores: a "Z:" prefix and single
// backslashes. QJsonDocument escapes each one, so on disk it reads
// "Z:\\home\\eamon\\...", matching what the launcher writes.
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
