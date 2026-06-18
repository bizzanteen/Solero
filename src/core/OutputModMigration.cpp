#include "OutputModMigration.h"
#include "ModList.h"
#include "StagingFolder.h"
#include "Types.h"
#include <QDir>
#include <QFile>
#include <QSet>
#include <QDebug>

namespace solero {

int migrateOutputModsProfileQualified(const QString& profilesRoot,
                                      const QString& stagingDir,
                                      const QStringList& processOrder) {
    // Lower-cased on-disk source folders already claimed (moved from) by an
    // earlier profile in this run - so a SHARED bare folder is consumed once and
    // later profiles get a fresh empty copy instead of stealing the same content.
    QSet<QString> claimedSources;
    int migrated = 0;

    for (const QString& profileName : processOrder) {
        const QString modlistPath =
            profilesRoot + "/" + profileName + "/modlist.json";
        if (!QFile::exists(modlistPath)) continue;

        ModList modList = ModList::loadFromFile(modlistPath);
        auto& entries = modList.entries();

        // Folder names already taken in this profile (case-insensitive), so newly
        // assigned profile-qualified names don't collide with each other.
        QSet<QString> taken;
        for (const auto& e : entries)
            if (e.type == EntryType::Mod && !e.stagingFolder.isEmpty())
                taken.insert(e.stagingFolder.toLower());

        bool changed = false;
        bool backedUp = false; // back up this profile's modlist once, lazily

        for (auto& e : entries) {
            if (e.type != EntryType::Mod || !e.isOutputMod) continue;

            const QString desired =
                sanitizeStagingFolder(profileName + " - " + e.name);

            // Already profile-qualified (its folder already starts with
            // "<Profile> - <name>", possibly with a "(N)" uniqueness suffix) -> skip
            // before computing a fresh unique name (which would otherwise always
            // differ and re-migrate it). Compare case-insensitively to match the
            // taken-set / uniqueStagingFolder semantics.
            if (e.stagingFolder.compare(desired, Qt::CaseInsensitive) == 0)
                continue;

            const QString target = uniqueStagingFolder(desired, taken);

            const QString currentFolder =
                e.stagingFolder.isEmpty() ? e.id : e.stagingFolder;
            const QString currentPath = stagingDir + "/" + currentFolder;
            const QString targetPath  = stagingDir + "/" + target;
            const QString claimKey    = currentFolder.toLower();

            const bool sourceExists = !stagingDir.isEmpty() && QDir(currentPath).exists();
            const bool sourceClaimed = claimedSources.contains(claimKey);

            if (!backedUp) {
                QFile::copy(modlistPath, modlistPath + ".bak-outputmodmigration");
                backedUp = true;
            }

            if (sourceExists && !sourceClaimed) {
                // Move the real content into the profile-qualified folder.
                if (QDir().rename(currentPath, targetPath)) {
                    claimedSources.insert(claimKey);
                    qInfo() << "outputModMigration:" << profileName
                            << "renamed" << currentFolder << "->" << target;
                } else {
                    qWarning() << "outputModMigration:" << profileName
                               << "rename failed" << currentFolder << "->" << target
                               << "; creating fresh folder instead";
                    QDir().mkpath(targetPath + "/Data");
                }
            } else {
                // Shared folder already claimed by an earlier profile, or the
                // source is missing: make a fresh empty folder that regenerates
                // on the next tool run.
                QDir().mkpath(targetPath + "/Data");
                qInfo() << "outputModMigration:" << profileName
                        << "created fresh folder" << target
                        << (sourceClaimed ? "(shared source already claimed)"
                                          : "(source missing)");
            }

            e.stagingFolder = target;
            taken.insert(target.toLower());
            changed = true;
            ++migrated;
        }

        if (changed)
            modList.saveToFile(modlistPath);
    }

    return migrated;
}

} // namespace solero
