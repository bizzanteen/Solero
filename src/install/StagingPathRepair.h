#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// One-time repair for mods staged before extraction expanded Windows path
// separators. Such a mod holds files whose NAMES contain backslashes
// ("Data/SKSE\Plugins\foo.dll") instead of the directory tree, so it deploys
// cleanly and does nothing. Re-installing is the only other cure, and nobody
// re-installs a mod that reports no problem, so existing setups would stay broken.
//
// Moves each affected file into its expanded path, per staging folder. Never
// deletes: an entry that cannot be made safe is left where it is and logged.
// `repaired` (optional) collects "<mod folder>/<new path>" for reporting.
// Returns the number of files moved.
int repairWindowsPathsInStaging(const QString& stagingDir, QStringList* repaired = nullptr);

} // namespace solero
