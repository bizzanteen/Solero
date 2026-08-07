#pragma once
#include "core/Types.h"
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>

namespace solero { class ProfileManager; class Profile; }

namespace solero {

// A tool discovered in the MO2 instance's ModOrganizer.ini [customExecutables]
// section. `binary` is resolved to the path it will live at after import (inside
// the staged instance / game-root overlay) when that can be determined, else the
// raw INI value. `args` is the configured argument string (may be empty).
struct ImportedTool {
    QString name;     // [customExecutables] title=  (e.g. "DynDOLOD")
    QString binary;   // resolved absolute binary path (post-import), or raw value
    QString args;     // configured arguments
};

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
    QList<ImportedTool> tools;  // tools from ModOrganizer.ini [customExecutables]
    QString errorMessage;
};

class Mo2Importer {
public:
    // Parse modlist.txt content into a Solero-ordered ModList (index0 = lowest priority).
    static QList<ModEntry> parseModlist(const QString& modlistTxt);

    // [customExecutables] -> ImportedTool, one per N\title / N\binary /
    // N\arguments group, in index order. instanceDir rebases %baseDir% and
    // instance-relative binaries; absolute existing paths are kept. Empty
    // title or binary is skipped.
    static QList<ImportedTool> parseCustomExecutables(const QString& iniContent,
                                                      const QString& instanceDir = {});

    // Full import: read <mo2ProfileDir>/modlist.txt + plugins.txt, stage each mod
    // folder from <mo2ModsDir>/<ModName> into stagingRoot, create a Solero profile.
    static Mo2ImportResult importProfile(const QString& mo2ProfileDir,
                                         const QString& mo2ModsDir,
                                         const QString& stagingRoot,
                                         ProfileManager& profiles,
                                         const QString& newProfileName,
                                         bool symlinkMods);

    // Import a whole MO2/Wabbajack instance: one Solero profile per MO2 profile,
    // with each unique mod folder staged once and shared across them.
    //   mo2InstanceDir : holds mods/, profiles/ and ModOrganizer.ini
    //   listTitle      : disambiguates profile names on collision
    static Mo2InstanceImportResult importInstance(const QString& mo2InstanceDir,
                                                  const QString& stagingRoot,
                                                  ProfileManager& profiles,
                                                  const QString& listTitle,
                                                  bool symlinkMods);

    // Stage a game-root overlay folder (StockGame / Game Root / Root), if there is
    // one, into a ModEntry whose files sit at the staging root so they deploy to
    // <game>/ rather than <game>/Data. Vanilla files and the Proton cache are
    // excluded. Empty-id entry when there's nothing to stage.
    static ModEntry stageGameRootOverlay(const QString& instanceDir,
                                         const QString& stagingRoot, bool symlink,
                                         QSet<QString>& taken);
};

} // namespace solero
