#include "DeployEngine.h"
#include "Linker.h"
#include "loot/LootSorter.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include <QDirIterator>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <sys/stat.h>

namespace {
// Returns true if both paths exist and reside on different filesystems
// (different st_dev). Returns false if either cannot be stat'd.
bool onDifferentFilesystems(const QString& a, const QString& b) {
    struct stat sa, sb;
    if (::stat(a.toLocal8Bit().constData(), &sa) != 0) return false;
    if (::stat(b.toLocal8Bit().constData(), &sb) != 0) return false;
    return sa.st_dev != sb.st_dev;
}
}

namespace solero {

DeployEngine::DeployEngine(const QString& gameDir, const QString& stagingRoot)
    : m_gameDir(QDir::cleanPath(gameDir)), m_stagingRoot(QDir::cleanPath(stagingRoot)) {}

QString DeployEngine::recordPath(const QString& gameDir) {
    return gameDir + "/" + DeployRecord::recordFilename();
}

QString DeployEngine::conflictIndexPath(const QString& profileDir) {
    return profileDir + "/conflicts.json";
}

DeployResult DeployEngine::deploy(Profile& profile, DeployMode mode, const std::function<void(int,int)>& onProgress) {
    DeployResult result;

    // Clean up any previous deployment first to avoid orphaned files
    undeploy(m_gameDir);

    // Seed per-file conflict rules from the profile (hidden files + forced winners).
    m_hiddenFiles   = profile.hiddenFiles();
    m_fileOverrides = profile.fileOverrides();

    Linker linker(mode);
    DeployRecord record;
    ConflictIndex conflicts;
    // Case-insensitive owner index (lowercased relPath -> actual deployed
    // relPath). Wine/Proton present the game dir case-insensitively, so two
    // mods staging the same path with different case must collapse to one file.
    QHash<QString, QString> ciOwners;

    int total = 0;
    for (const auto& entry : profile.modList())
        if (entry.type == EntryType::Mod && entry.enabled) ++total;
    total += 1; // finalize/LOOT step
    int done = 0;
    if (onProgress) onProgress(0, total);

    int failures = 0;
    for (const auto& entry : profile.modList()) {
        if (entry.type != EntryType::Mod) continue;
        if (!entry.enabled) continue;
        failures += deployMod(entry.id, m_gameDir, linker, record, conflicts, ciOwners);
        ++done;
        if (onProgress) onProgress(done, total);
    }

    // Force per-path winners (Vortex/MO2 per-file conflict choice). Runs after the
    // normal load-order loop so a chosen mod wins regardless of its priority.
    applyWinnerOverrides(profile, m_gameDir, linker, record, conflicts, ciOwners);

    // Deploy the Overwrite folder last so it wins every conflict (MO2-style: the
    // capture/Overwrite layer sits above all mods). Its contents are Data-relative.
    {
        const QString overwriteDir = m_overwriteDir.isEmpty()
            ? (AppConfig::dataRoot() + "/overwrite") : m_overwriteDir;
        failures += deployOverwrite(m_gameDir, overwriteDir, linker, record, conflicts, ciOwners);
    }

    // Sort plugins with LOOT (if enabled) before writing plugins.txt. The mod
    // files must already be deployed above so LOOT can read the plugin headers.
    // A locked load order skips the auto-sort entirely and deploys the current
    // manual order as-is.
    if (m_lootEnabled && !profile.pluginList().loadOrderLocked()) {
        auto sortResult = LootSorter::sort(
            profile.pluginList(),
            m_gameDir,
            m_userlistPath.isEmpty() ? profile.lootUserlistPath() : m_userlistPath);
        if (!sortResult.success)
            qWarning() << "LOOT sort failed (deploying with current order):"
                       << sortResult.errorMessage;
        profile.pluginList().applyPins(); // restore pinned plugins after the sort
        profile.save(); // persist the sorted order back to the profile
    }

    // Plugins.txt belongs in the game's local appdata folder (inside the Proton
    // prefix), where Skyrim actually reads it. Fall back to Data/ if unknown.
    QString localAppData = AppConfig::instance().localAppDataDir();
    QString pluginsDir = localAppData.isEmpty() ? (m_gameDir + "/Data") : localAppData;
    QDir().mkpath(pluginsDir);
    profile.pluginList().saveToFile(pluginsDir + "/Plugins.txt");
    // loadorder.txt records the full order (all plugins, no prefix) so the game
    // and tools agree on the master/light/esp ordering, not just what's active.
    profile.pluginList().saveLoadOrderToFile(pluginsDir + "/loadorder.txt");

    // INIs belong in the game's My Games documents folder. Fall back to gameDir.
    QString docsDir = AppConfig::instance().documentsDir();
    QString iniDir = docsDir.isEmpty() ? m_gameDir : docsDir;
    QDir().mkpath(iniDir);
    const QStringList inis = {
        profile.skyrimIniPath(),
        profile.skyrimPrefsPath(),
        profile.skyrimCustomPath()
    };
    const QStringList iniTargets = {
        iniDir + "/Skyrim.ini",
        iniDir + "/SkyrimPrefs.ini",
        iniDir + "/SkyrimCustom.ini"
    };
    for (int i = 0; i < inis.size(); ++i) {
        if (QFile::exists(inis[i])) {
            QFile::remove(iniTargets[i]);
            QFile::copy(inis[i], iniTargets[i]);
        }
    }

    // JContainers compatibility: it crashes at load (boost::filesystem
    // directory_iterator::construct) if Data/SKSE/Plugins/JCData/Domains is
    // missing. MO2's VFS provides that folder virtually, but the mod doesn't ship
    // it and a file-only deploy can't create an empty directory - so create it
    // when JContainers is present. (ryobg/JContainers#38.)
    {
        const QString jcData = m_gameDir + "/Data/SKSE/Plugins/JCData";
        if (QDir(jcData).exists()) QDir().mkpath(jcData + "/Domains");
    }

    record.saveToFile(recordPath(m_gameDir));
    conflicts.saveToFile(conflictIndexPath(profile.path()));

    if (onProgress) onProgress(total, total);

    result.success = (failures == 0);
    if (failures > 0)
        result.errorMessage = QString("%1 file(s) failed to deploy; "
                                      "the rest were deployed.").arg(failures);

    // Cross-filesystem hardlinks silently degrade to copies. Warn so the user
    // knows they're paying extra disk and not getting hardlink semantics.
    if (mode == DeployMode::HardLink
        && onDifferentFilesystems(m_stagingRoot, m_gameDir)) {
        result.warning = "Staging and game dir are on different filesystems "
                         "- deployed as copies (uses extra disk), not hardlinks.";
    }

    result.conflicts = std::move(conflicts);
    result.filesDeployed = record.count();
    return result;
}

int DeployEngine::deployMod(const QString& modId,
                             const QString& gameDir,
                             const Linker& linker,
                             DeployRecord& record,
                             ConflictIndex& conflicts,
                             QHash<QString, QString>& ciOwners) {
    QString modRoot = m_stagingRoot + "/" + modId;
    if (!QDir(modRoot).exists()) return 0;

    int failures = 0;
    QDirIterator it(modRoot, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        QString srcPath = it.next();
        QString relPath = srcPath.mid(modRoot.length() + 1);
        QString dstPath = gameDir + "/" + relPath;

        // Skip per-mod metadata: the hidden .solero marker(s) and any legacy
        // fomod-choices.json. These live in the staging root and must never
        // deploy (they would collide across mods in the game dir).
        QString fn = QFileInfo(srcPath).fileName();
        if (fn.startsWith(".solero")
            || fn.compare("fomod-choices.json", Qt::CaseInsensitive) == 0)
            continue;

        // Per-file HIDE (MO2 ".mohidden"): this mod no longer provides the file,
        // so don't link or record it - the next-priority provider then wins.
        {
            auto hit = m_hiddenFiles.constFind(modId);
            if (hit != m_hiddenFiles.constEnd() && hit.value().contains(relPath))
                continue;
        }

        const QString ciKey = relPath.toLower();
        const QString priorRel = ciOwners.value(ciKey);   // earlier-deployed path at this CI path, if any
        QString previousOwner = priorRel.isEmpty() ? QString() : record.ownerOf(priorRel);

        // Wine/Proton is case-insensitive: a file already deployed at a case-variant
        // of this path (e.g. QUI.dll vs qui.dll) would both be visible to the game and
        // load twice. The current mod is later in the load order = higher priority =
        // winner, so drop the stale variant and let this file take its place.
        if (!priorRel.isEmpty() && priorRel != relPath) {
            QFile::remove(gameDir + "/" + priorRel);
            record.remove(priorRel);
        }

        // First Solero owner of this path and something already lives there:
        // it's a genuine pre-existing original (deploy() ran undeploy() first,
        // so any Solero-owned file at this path is already gone). Move it to
        // the backup tree so undeploy can restore it later.
        if (previousOwner.isEmpty() && QFile::exists(dstPath)) {
            QString backupPath = gameDir + "/" + backupDirName() + "/" + relPath;
            // Don't clobber an existing backup (e.g. from an interrupted run);
            // the first one we saw is the true original.
            if (!QFile::exists(backupPath)) {
                QDir().mkpath(QFileInfo(backupPath).path());
                if (!QFile::rename(dstPath, backupPath)) {
                    // Cross-fs rename can fail; fall back to copy + remove.
                    if (QFile::copy(dstPath, backupPath))
                        QFile::remove(dstPath);
                }
            }
        }

        // Resolve symlinks so we hardlink/copy the real file. Symlink-staged mods
        // (and the StockGame overlay) point back into their source; deploying the
        // symlink itself makes executables like skse64_loader.exe run FROM that
        // source dir and launch the wrong game instance (StockGame's vanilla copy).
        QString realSrc = QFileInfo(srcPath).canonicalFilePath();
        if (realSrc.isEmpty()) realSrc = srcPath;
        if (!linker.deploy(realSrc, dstPath)) {
            qWarning() << "Deploy failed for" << relPath << "(mod" << modId << ")";
            ++failures;
            continue; // do not record a failed link as deployed
        }

        if (!previousOwner.isEmpty()) {
            conflicts.recordConflict(relPath, modId, previousOwner);
        } else {
            conflicts.setWinner(relPath, modId);
        }
        record.add(relPath, modId);
        ciOwners.insert(ciKey, relPath);
    }
    return failures;
}

void DeployEngine::applyWinnerOverrides(Profile& profile,
                                        const QString& gameDir,
                                        const Linker& linker,
                                        DeployRecord& record,
                                        ConflictIndex& conflicts,
                                        QHash<QString, QString>& ciOwners) {
    for (auto it = m_fileOverrides.cbegin(); it != m_fileOverrides.cend(); ++it) {
        const QString relPath = it.key();
        const QString modId   = it.value();
        if (modId.isEmpty()) continue;

        // The forced winner must be an enabled mod in the current load order.
        const ModEntry* entry = profile.modList().findById(modId);
        if (!entry || entry->type != EntryType::Mod || !entry->enabled) {
            qWarning() << "Winner override skipped: mod" << modId
                       << "is missing/disabled for" << relPath;
            continue;
        }
        // A hidden file isn't provided by the mod - never override to it.
        {
            auto hit = m_hiddenFiles.constFind(modId);
            if (hit != m_hiddenFiles.constEnd() && hit.value().contains(relPath))
                continue;
        }
        // Validate the mod actually stages this path.
        const QString srcPath = m_stagingRoot + "/" + modId + "/" + relPath;
        if (!QFile::exists(srcPath)) {
            qWarning() << "Winner override skipped: mod" << modId
                       << "does not provide" << relPath;
            continue;
        }

        const QString ciKey  = relPath.toLower();
        const QString priorRel = ciOwners.value(ciKey);
        const QString currentOwner = priorRel.isEmpty() ? QString() : record.ownerOf(priorRel);
        if (currentOwner == modId && priorRel == relPath)
            continue; // already the winner at this exact path - nothing to do

        const QString dstPath = gameDir + "/" + relPath;

        // Drop any stale case-variant the load-order loop deployed (Wine/Proton is
        // case-insensitive, so both would otherwise be visible to the game).
        if (!priorRel.isEmpty() && priorRel != relPath) {
            QFile::remove(gameDir + "/" + priorRel);
            record.remove(priorRel);
        }

        // No Solero owner yet but a file sits at the slot -> it's a pre-existing
        // original; back it up so undeploy can restore it (mirrors deployMod).
        if (currentOwner.isEmpty() && QFile::exists(dstPath)) {
            QString backupPath = gameDir + "/" + backupDirName() + "/" + relPath;
            if (!QFile::exists(backupPath)) {
                QDir().mkpath(QFileInfo(backupPath).path());
                if (!QFile::rename(dstPath, backupPath)) {
                    if (QFile::copy(dstPath, backupPath))
                        QFile::remove(dstPath);
                }
            }
        }

        QString realSrc = QFileInfo(srcPath).canonicalFilePath();
        if (realSrc.isEmpty()) realSrc = srcPath;
        if (!linker.deploy(realSrc, dstPath)) {
            qWarning() << "Winner override failed to link" << relPath << "(mod" << modId << ")";
            continue;
        }
        // The displaced load-order winner becomes a loser of the forced choice.
        if (!currentOwner.isEmpty() && currentOwner != modId)
            conflicts.recordConflict(relPath, modId, currentOwner);
        else
            conflicts.setWinner(relPath, modId);
        record.add(relPath, modId);
        ciOwners.insert(ciKey, relPath);
    }
}

int DeployEngine::deployOverwrite(const QString& gameDir,
                                  const QString& overwriteDir,
                                  const Linker& linker,
                                  DeployRecord& record,
                                  ConflictIndex& conflicts,
                                  QHash<QString, QString>& ciOwners) {
    if (overwriteDir.isEmpty() || !QDir(overwriteDir).exists()) return 0;
    const QString owner = overwriteOwnerId();

    int failures = 0;
    QDirIterator it(overwriteDir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        QString srcPath = it.next();

        // Skip Solero metadata markers; they must never deploy.
        if (QFileInfo(srcPath).fileName().startsWith(".solero")) continue;

        // The Overwrite folder is Data-relative: a file at <overwrite>/R belongs at
        // gameDir/Data/R and is recorded gameDir-relative ("Data/R"), exactly like
        // every mod file (which stages Data/... and records "Data/...").
        const QString r       = srcPath.mid(overwriteDir.length() + 1);
        const QString relPath = "Data/" + r;
        const QString dstPath = gameDir + "/Data/" + r;

        const QString ciKey = relPath.toLower();
        const QString priorRel = ciOwners.value(ciKey);   // earlier-deployed path at this CI path, if any
        QString previousOwner = priorRel.isEmpty() ? QString() : record.ownerOf(priorRel);

        // Overwrite wins everything: drop any case-variant a mod already deployed
        // (Wine/Proton is case-insensitive, so both would otherwise be visible).
        if (!priorRel.isEmpty() && priorRel != relPath) {
            QFile::remove(gameDir + "/" + priorRel);
            record.remove(priorRel);
        }

        // First Solero owner here and a file already sits there: it's a genuine
        // pre-existing original - back it up so undeploy can restore it (mirrors
        // deployMod). Mod-owned files are left to be overwritten in place.
        if (previousOwner.isEmpty() && QFile::exists(dstPath)) {
            QString backupPath = gameDir + "/" + backupDirName() + "/" + relPath;
            if (!QFile::exists(backupPath)) {
                QDir().mkpath(QFileInfo(backupPath).path());
                if (!QFile::rename(dstPath, backupPath)) {
                    if (QFile::copy(dstPath, backupPath))
                        QFile::remove(dstPath);
                }
            }
        }

        // Resolve symlinks so we hardlink/copy the real file (see deployMod).
        QString realSrc = QFileInfo(srcPath).canonicalFilePath();
        if (realSrc.isEmpty()) realSrc = srcPath;
        if (!linker.deploy(realSrc, dstPath)) {
            qWarning() << "Overwrite deploy failed for" << relPath;
            ++failures;
            continue; // do not record a failed link as deployed
        }

        // Overwrite wins: any prior owner becomes a loser of this path.
        if (!previousOwner.isEmpty() && previousOwner != owner)
            conflicts.recordConflict(relPath, owner, previousOwner);
        else
            conflicts.setWinner(relPath, owner);
        record.add(relPath, owner);
        ciOwners.insert(ciKey, relPath);
    }
    return failures;
}

void DeployEngine::deployOverwriteIncremental(const QString& gameDir, DeployMode mode,
                                              const QString& overwriteDir) {
    const QString owDir = overwriteDir.isEmpty()
        ? (AppConfig::dataRoot() + "/overwrite") : overwriteDir;
    if (owDir.isEmpty() || !QDir(owDir).exists()) return;

    const QString normGameDir = QDir::cleanPath(gameDir);
    DeployRecord record = DeployRecord::loadFromFile(recordPath(normGameDir));
    Linker linker(mode);
    const QString owner = overwriteOwnerId();

    bool changed = false;
    QDirIterator it(owDir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        QString srcPath = it.next();
        if (QFileInfo(srcPath).fileName().startsWith(".solero")) continue;

        const QString r       = srcPath.mid(owDir.length() + 1);
        const QString relPath = "Data/" + r;
        const QString dstPath = normGameDir + "/Data/" + r;

        QString realSrc = QFileInfo(srcPath).canonicalFilePath();
        if (realSrc.isEmpty()) realSrc = srcPath;
        if (linker.deploy(realSrc, dstPath)) {     // relinking is idempotent
            record.add(relPath, owner);
            changed = true;
        }
    }
    if (changed) record.saveToFile(recordPath(normGameDir));
}

bool DeployEngine::undeploy(const QString& gameDir, const std::function<void(int,int)>& onProgress) {
    const QString normGameDir = QDir::cleanPath(gameDir);
    QString recPath = recordPath(normGameDir);
    DeployRecord record = DeployRecord::loadFromFile(recPath);

    const QString backupRoot = normGameDir + "/" + backupDirName();

    auto paths = record.allPaths();
    int total = paths.size();
    int done = 0;
    for (const auto& relPath : paths) {
        QString fullPath = normGameDir + "/" + relPath;
        QFile::remove(fullPath);

        // Prune now-empty parent dirs up to (but not including) the game dir.
        // Never descend into / prune the backup tree.
        QDir dir(QFileInfo(fullPath).path());
        while (QDir::cleanPath(dir.path()) != normGameDir
               && !QDir::cleanPath(dir.path()).startsWith(backupRoot)
               && dir.exists() && dir.isEmpty()) {
            QString dirPath = dir.path();
            QString parent = QFileInfo(dirPath).path();
            // Remove the named dir from its parent (portable; rmdir(".") is not).
            QDir(parent).rmdir(QFileInfo(dirPath).fileName());
            dir = QDir(parent);
        }
        ++done;
        if (onProgress && (done % 20 == 0 || done == total)) onProgress(done, total);
    }

    // Restore pre-existing originals that mods were deployed over. The backup
    // tree mirrors relPaths, so it's self-describing - move each file back to
    // its game-dir slot (overwriting the just-removed deployed file's place),
    // then tear down the backup tree.
    if (QDir(backupRoot).exists()) {
        QDirIterator bit(backupRoot, QDir::Files | QDir::NoDotAndDotDot,
                         QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (bit.hasNext()) {
            QString backupPath = bit.next();
            QString relPath = backupPath.mid(backupRoot.length() + 1);
            QString dstPath = normGameDir + "/" + relPath;
            QDir().mkpath(QFileInfo(dstPath).path());
            QFile::remove(dstPath); // ensure the slot is free
            if (!QFile::rename(backupPath, dstPath)) {
                // Cross-fs fallback.
                if (QFile::copy(backupPath, dstPath))
                    QFile::remove(backupPath);
            }
        }
        QDir(backupRoot).removeRecursively();
    }

    QFile::remove(recPath);
    return true;
}

} // namespace solero
