#include "StagingPathRepair.h"
#include "ArchiveTool.h"
#include "core/Log.h"
#include <QDir>

namespace solero {

int repairWindowsPathsInStaging(const QString& stagingDir, QStringList* repaired) {
    if (stagingDir.isEmpty() || !QDir(stagingDir).exists()) return 0;

    int total = 0;
    const QStringList folders =
        QDir(stagingDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QString& folder : folders) {
        QStringList expanded;
        // Scoped per mod folder so a repaired path is always relative to the mod,
        // exactly as it would have been had the archive extracted correctly.
        if (!ArchiveTool::expandWindowsPaths(stagingDir + "/" + folder, &expanded))
            qCWarning(lcInstall) << "staging repair: could not fully expand" << folder;
        if (expanded.isEmpty()) continue;
        total += expanded.size();
        qCInfo(lcInstall) << "staging repair:" << folder << "had" << expanded.size()
                          << "file(s) with Windows path separators in their names";
        if (repaired)
            for (const QString& e : expanded) *repaired << folder + "/" + e;
    }
    return total;
}

} // namespace solero
