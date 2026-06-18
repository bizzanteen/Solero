#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// One-time migration: rename existing per-profile output-mod staging folders to be
// profile-qualified ("<Profile> - <name>") so two profiles can't collide on the same
// bare folder. Renames and fresh mkdirs only, never deletes; processOrder lists the
// profile that keeps the shared content first. Returns the number of mods migrated.
int migrateOutputModsProfileQualified(const QString& profilesRoot,
                                      const QString& stagingDir,
                                      const QStringList& processOrder);

} // namespace solero
