#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// One-time repair of mods staged before extraction expanded Windows separators,
// i.e. files literally named "Data/SKSE\Plugins\foo.dll". Never deletes; an
// unplaceable name is left alone. `repaired` collects "<folder>/<new path>".
int repairWindowsPathsInStaging(const QString& stagingDir, QStringList* repaired = nullptr);

} // namespace solero
