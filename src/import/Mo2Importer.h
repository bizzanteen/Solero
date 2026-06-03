#pragma once
#include "core/Types.h"
#include <QString>
#include <QList>

namespace solero { class ProfileManager; }

namespace solero {

struct Mo2ImportResult {
    bool success = false;
    QString profileName;
    int modsStaged = 0;
    QString errorMessage;
};

class Mo2Importer {
public:
    // Parse modlist.txt content into a Solero-ordered ModList (index0 = lowest priority).
    static QList<ModEntry> parseModlist(const QString& modlistTxt);

    // Full import: read <mo2ProfileDir>/modlist.txt + plugins.txt, stage each mod
    // folder from <mo2ModsDir>/<ModName> into stagingRoot, create a Solero profile.
    static Mo2ImportResult importProfile(const QString& mo2ProfileDir,
                                         const QString& mo2ModsDir,
                                         const QString& stagingRoot,
                                         ProfileManager& profiles,
                                         const QString& newProfileName,
                                         bool symlinkMods);
};

} // namespace solero
