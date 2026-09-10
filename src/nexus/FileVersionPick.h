#pragma once
#include "NexusApi.h"
#include "core/VersionUtil.h"

namespace solero {

// Index of the file that best matches an installed version, or 0 when nothing
// lines up. Used to preselect a row when identifying a mod by name: the version
// Solero recorded is the strongest hint about which file is actually installed,
// and picking the newest MAIN instead is how you end up re-downloading a
// different mod than the one on disk.
inline int bestFileIndexForVersion(const QList<NexusApi::NexusFile>& files,
                                   const QString& installedVersion) {
    if (files.isEmpty()) return 0;
    if (!installedVersion.isEmpty()) {
        for (int i = 0; i < files.size(); ++i)
            if (files.at(i).version.compare(installedVersion, Qt::CaseInsensitive) == 0)
                return i;
    }
    // No version match: fall back to the newest MAIN file, else the first row.
    int best = -1;
    for (int i = 0; i < files.size(); ++i) {
        const auto& f = files.at(i);
        if (f.category.compare(QLatin1String("MAIN"), Qt::CaseInsensitive) != 0) continue;
        if (best < 0 || isVersionNewer(files.at(best).version, f.version)) best = i;
    }
    return best < 0 ? 0 : best;
}

} // namespace solero
