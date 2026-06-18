#pragma once
#include <QString>
#include <QStringList>

namespace solero {
class Profile;
class ModList;
struct ModEntry;

// Prepares Radium Textures for Solero's deployed load order: regenerates
// <installDir>/fake-mo2/ (a MO2-shaped instance built from the profile) and
// merge-writes Radium's settings.json (manual_mode + paths), preserving the user's
// preset/thread/GPU tuning.
namespace RadiumPrep {

// Production config target: ~/.config/radium-textures/settings.json
QString defaultSettingsPath();

// Build the MO2 modlist.txt lines (top line = highest priority, reverse of Solero's
// index order). Mods render as "+folder"/"-folder"; separators as
// "-<name>_separator". Each line's folder token must match a mods/<folder> entry.
QStringList buildMo2Modlist(const ModList& modList);

// On-disk mods/ folder token for one entry (the bare folder name, no +/- prefix).
QString mo2ModFolder(const ModEntry& e);

// Generates a fake-MO2 instance at fakeMo2Dir from the profile's load order
// (mods/, profiles/solero/ with loadorder/archives/modlist/plugins, ModOrganizer.ini)
// for any MO2-expecting tool. gameDir's Data/ is scanned for .bsa. Returns false and
// sets *error on I/O failure.
// populateMods (PGPatcher-only): symlink each mod into mods/ so MO2 mode can detect
// inter-mod conflicts; the staging root then comes from AppConfig. Radium leaves it
// false and works with an empty mods/.
bool writeFakeMo2(const Profile& profile,
                  const QString& gameDir,
                  const QString& fakeMo2Dir,
                  QString* error,
                  bool populateMods = false);

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
