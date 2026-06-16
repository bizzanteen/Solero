#pragma once
#include <QString>

namespace solero {
class Profile;

// Prepares Radium Textures to optimize Solero's deployed load order:
//  - regenerates <installDir>/fake-mo2/ (empty mods/, profile with
//    loadorder.txt from the profile's plugin order + archives.txt from Data/*.bsa)
//  - merge-writes Radium's settings.json with manual_mode + explicit paths,
//    preserving the user's preset/thread/GPU tuning.
namespace RadiumPrep {

// Production config target: ~/.config/radium-textures/settings.json
QString defaultSettingsPath();

// Generates a fake-MO2 instance at fakeMo2Dir from the profile's load order:
//  - empty mods/ dir
//  - profiles/solero/{loadorder.txt (enabled plugins), archives.txt (Data/*.bsa
//    sorted), modlist.txt}
//  - ModOrganizer.ini at the root (gameName/gamePath/selected_profile).
// Reusable by any MO2-expecting tool (Radium, PGPatcher). gameDir is the game
// root (its Data/ is scanned for .bsa). Returns false (and sets *error) on I/O
// failure.
bool writeFakeMo2(const Profile& profile,
                  const QString& gameDir,
                  const QString& fakeMo2Dir,
                  QString* error);

// Returns false (and sets *error, if non-null) on any I/O failure.
// settingsPath is injectable so tests can target a temp file.
bool prepare(const Profile& profile,
             const QString& gameDir,
             const QString& installDir,
             const QString& outputDataDir,
             const QString& settingsPath,
             QString* error);

} // namespace RadiumPrep
} // namespace solero
