#pragma once
#include "core/Types.h"
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>

namespace solero { class ProfileManager; class Profile; }

namespace solero {

struct Mo2ImportResult {
    bool success = false;
    QString profileName;
    int modsStaged = 0;
    QString errorMessage;
};

struct Mo2InstanceImportResult {
    bool success = false;
    QStringList profileNames;   // created Solero profile names, in instance order
    QString primaryProfile;     // the one to switch to (MO2 selected_profile if found, else first)
    int modsStaged = 0;         // unique mods staged
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

    // Import an ENTIRE installed MO2/Wabbajack instance: create one Solero profile
    // per MO2 profile (each has its own load order / enabled set / separators),
    // while staging each unique referenced mod folder exactly once and sharing it
    // (same Solero mod id) across all created profiles.
    //   mo2InstanceDir : dir containing mods/ + profiles/ + ModOrganizer.ini
    //   listTitle      : used to disambiguate Solero profile names on collision
    static Mo2InstanceImportResult importInstance(const QString& mo2InstanceDir,
                                                  const QString& stagingRoot,
                                                  ProfileManager& profiles,
                                                  const QString& listTitle,
                                                  bool symlinkMods);

    // If `instanceDir` contains a game-root overlay folder (StockGame / "Stock Game" /
    // "Game Root" / "Root"), stage its mod-added root-level loose files (dll/exe/asi/ini,
    // excluding vanilla game files + Proton cache) into a new ModEntry whose files sit at
    // the staging root (so they deploy to <game>/, not <game>/Data). Returns an invalid/
    // empty-id ModEntry if there's no overlay or nothing mod-added to stage.
    static ModEntry stageGameRootOverlay(const QString& instanceDir,
                                         const QString& stagingRoot, bool symlink);
};

} // namespace solero
