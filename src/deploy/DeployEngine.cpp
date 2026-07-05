#include "DeployEngine.h"
#include "Linker.h"
#include "loot/LootSorter.h"
#include "core/Profile.h"
#include "core/ShaderCache.h"
#include "core/AppConfig.h"
#include "core/StagingFolder.h"
#include "core/FileUtil.h"
#include "core/Log.h"
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

// Sentinel deploy-record owner for engine-generated artifacts (Plugins.txt,
// loadorder.txt, copied INIs) that land inside gameDir. Lets undeploy() remove
// them like any other recorded path without attributing them to a real mod.
const QString kGeneratedArtifactOwner = QStringLiteral("__solero_generated__");
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

    // Reset the per-deploy directory case-folding caches. canonicalizeRelPath()
    // populates these as it walks the (post-undeploy) live game dir, so all mods
    // funnel into a single on-disk casing per directory.
    m_dirCaseIndex.clear();
    m_dirCaseScanned.clear();

    int total = 0;
    for (const auto& entry : profile.modList())
        if (entry.type == EntryType::Mod && entry.enabled) ++total;
    qCInfo(lcDeploy) << "deploy start:" << total << "enabled mods, mode" << int(mode)
                     << ", staging" << m_stagingRoot;
    // Counts one step only when a cache folder for the active CS version will
    // actually deploy (below) - mirrors that guard so progress isn't off by one.
    if (profile.shaderCache().managed
        && !profile.shaderCache().folderFor(activeCacheKey(profile.modList())).isEmpty())
        ++total; // deployed last, below
    total += 1; // finalize/LOOT step
    int done = 0;
    if (onProgress) onProgress(0, total);

    ///: persist the record INCREMENTALLY (flush after each mod, not each
    // file) so that at any interruption point the on-disk record covers exactly
    // what has been linked into the game dir - a crash mid-deploy then leaves a
    // record that undeploy() can fully act on, never orphaning files. Track save
    // failures so the caller can warn (a live-but-unrecorded deploy is unsafe).
    const QString recPath = recordPath(m_gameDir);
    bool recordSaveFailed = false;
    auto flushRecord = [&]() {
        if (!record.saveToFile(recPath)) {
            recordSaveFailed = true;
            qCWarning(lcDeploy) << "failed to persist deploy record to" << recPath;
        }
    };

    int failures = 0;
    // Deploy every enabled mod in list order.
    for (const auto& entry : profile.modList()) {
        if (entry.type != EntryType::Mod) continue;
        if (!entry.enabled) continue;
        failures += deployMod(entry, m_gameDir, linker, record, conflicts, ciOwners);
        flushRecord();
        ++done;
        if (onProgress) onProgress(done, total);
    }
    // The managed shader cache (profile-level state, not a list entry) deploys after
    // everything else so its captured shaders win all conflicts (last-wins). Applied
    // via a synthetic ModEntry pointing at the active CS version's staging folder.
    if (profile.shaderCache().managed) {
        const QString folder =
            profile.shaderCache().folderFor(activeCacheKey(profile.modList()));
        if (!folder.isEmpty()) {
            ModEntry cacheEntry;
            cacheEntry.type          = EntryType::Mod;
            cacheEntry.id            = QStringLiteral("__shadercache__");
            cacheEntry.name          = QStringLiteral("Shader Cache");
            cacheEntry.enabled       = true;
            cacheEntry.stagingFolder = folder;
            failures += deployMod(cacheEntry, m_gameDir, linker, record, conflicts, ciOwners);
            flushRecord();
            ++done;
            if (onProgress) onProgress(done, total);
        }
        // No folder for this CS version -> deploy no cache at all. CS compiles fresh
        // and the post-play capture populates this key. Never deploy a different
        // version's cache.
    }

    // Force per-path winners (Vortex/MO2 per-file conflict choice). Runs after the
    // normal load-order loop so a chosen mod wins regardless of its priority.
    failures += applyWinnerOverrides(profile, m_gameDir, linker, record, conflicts, ciOwners);
    flushRecord();

    // Sort plugins with LOOT (if enabled) before writing plugins.txt. The mod
    // files must already be deployed above so LOOT can read the plugin headers.
    // A locked load order skips the auto-sort entirely and deploys the current
    // manual order as-is.
    QString lootSortError; // non-empty => append to the deploy warning below
    if (m_lootEnabled && !profile.pluginList().loadOrderLocked()) {
        auto sortResult = LootSorter::sort(
            profile.pluginList(),
            m_gameDir,
            m_userlistPath.isEmpty() ? profile.lootUserlistPath() : m_userlistPath);
        if (!sortResult.success) {
            qCWarning(lcDeploy) << "LOOT sort failed (deploying with current order):"
                                << sortResult.errorMessage;
            lootSortError = sortResult.errorMessage;
        }
        profile.pluginList().applyPins(); // restore pinned plugins after the sort
        profile.save(); // persist the sorted order back to the profile
    }

    // Plugins.txt belongs in the game's local appdata folder (inside the Proton
    // prefix), where Skyrim actually reads it. Fall back to Data/ if unknown.
    // Record generated artifacts (Plugins.txt/loadorder.txt, copied INIs) under a
    // sentinel owner WHEN they land inside gameDir (the appdata/docs-not-found
    // fallback), so undeploy() removes them instead of orphaning them. Artifacts
    // written into the Proton prefix (localAppData/docs) are outside gameDir and
    // are intentionally not recorded (their relPath isn't gameDir-relative).
    const QString normGameDirForRec = QDir::cleanPath(m_gameDir);
    auto recordIfInGameDir = [&](const QString& target) {
        const QString norm = QDir::cleanPath(target);
        if (norm.startsWith(normGameDirForRec + "/")) {
            const QString rel = norm.mid(normGameDirForRec.length() + 1);
            record.add(rel, kGeneratedArtifactOwner);
        }
    };

    QString localAppData = AppConfig::instance().localAppDataDir();
    QString pluginsDir = localAppData.isEmpty() ? (m_gameDir + "/Data") : localAppData;
    QDir().mkpath(pluginsDir);
    profile.pluginList().saveToFile(pluginsDir + "/Plugins.txt");
    // loadorder.txt records the full order (all plugins, no prefix) so the game
    // and tools agree on the master/light/esp ordering, not just what's active.
    profile.pluginList().saveLoadOrderToFile(pluginsDir + "/loadorder.txt");
    recordIfInGameDir(pluginsDir + "/Plugins.txt");
    recordIfInGameDir(pluginsDir + "/loadorder.txt");

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
            copyOverwrite(inis[i], iniTargets[i]);
            recordIfInGameDir(iniTargets[i]);
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

    // Final flush also captures the generated artifacts recorded above (Plugins.txt/
    // loadorder.txt/INIs when they land inside gameDir).: check both saves.
    flushRecord();
    if (!conflicts.saveToFile(conflictIndexPath(profile.path()))) {
        recordSaveFailed = true;
        qCWarning(lcDeploy) << "failed to persist conflict index to"
                            << conflictIndexPath(profile.path());
    }

    if (onProgress) onProgress(total, total);

    result.success = (failures == 0 && !recordSaveFailed);
    if (failures > 0)
        result.errorMessage = QString("%1 file(s) failed to deploy; "
                                      "the rest were deployed.").arg(failures);
    if (recordSaveFailed) {
        if (!result.errorMessage.isEmpty()) result.errorMessage += "\n\n";
        result.errorMessage += "The deploy record could not be saved - the "
                               "deployment may not be fully tracked, so undeploy "
                               "could leave files behind. Check disk space and "
                               "folder permissions, then re-deploy.";
    }

    auto appendWarning = [&](const QString& msg) {
        if (msg.isEmpty()) return;
        if (!result.warning.isEmpty()) result.warning += "\n\n";
        result.warning += msg;
    };

    // Cross-filesystem hardlinks silently degrade to copies. Warn so the user
    // knows they're paying extra disk and not getting hardlink semantics.
    if (mode == DeployMode::HardLink
        && onDifferentFilesystems(m_stagingRoot, m_gameDir)) {
        appendWarning("Staging and game dir are on different filesystems "
                      "- deployed as copies (uses extra disk), not hardlinks.");
    }

    // LOOT couldn't sort the load order - the mods deployed fine, but plugins are
    // in their previous manual order, which may be wrong. Surface it so the user
    // can fix the cause (usually a bad rule in the LOOT userlist) and re-sort.
    if (!lootSortError.isEmpty())
        appendWarning("Could not sort plugins with LOOT - the load order was left "
                      "as it was. Your mods still deployed. Check your LOOT rules, "
                      "then use Sort Now to try again.\n\nDetails: " + lootSortError);

    const int conflictCount = conflicts.conflictedPaths().size();
    result.conflicts = std::move(conflicts);
    result.filesDeployed = record.count();
    qCInfo(lcDeploy) << "deploy done:" << result.filesDeployed << "files linked,"
                     << conflictCount << "conflicts," << failures << "failures";
    return result;
}

int DeployEngine::deployMod(const ModEntry& mod,
                             const QString& gameDir,
                             const Linker& linker,
                             DeployRecord& record,
                             ConflictIndex& conflicts,
                             QHash<QString, QString>& ciOwners) {
    // The id remains the deploy-record owner key; the on-disk folder is resolved
    // via the (name-based) staging folder.
    const QString modId = mod.id;
    QString modRoot = stagingPathFor(m_stagingRoot, mod);
    if (!QDir(modRoot).exists()) return 0;

    int failures = 0;
    QDirIterator it(modRoot, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        QString srcPath = it.next();
        QString relPath = srcPath.mid(modRoot.length() + 1);

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

        // Resolve the destination against existing on-disk directory casing so
        // all mods funnel into a single case per directory (Wine/Proton is
        // case-insensitive). Also lets the backup check below find a pre-existing
        // vanilla file of differing case. ciKey stays the lowercased relPath
        // (== destRel.toLower()); conflict/hidden rules keep the staged relPath.
        const QString destRel = canonicalizeRelPath(gameDir, relPath);
        QString dstPath = gameDir + "/" + destRel;

        const QString ciKey = relPath.toLower();
        const QString priorRel = ciOwners.value(ciKey);   // earlier-deployed path at this CI path, if any
        QString previousOwner = priorRel.isEmpty() ? QString() : record.ownerOf(priorRel);

        // The conflict index is keyed by the CANONICAL case-folded path so a
        // case-variant collision (QUI.dll vs qui.dll) maps to one conflict entry,
        // matching the case-insensitive on-disk collapse. Keying by the raw staged
        // relPath would split a case-variant pair into two entries and leave a stale
        // setWinner(loser) under the variant the Conflicts tab then shows winning.
        const QString conflictKey = ciKey;

        // Wine/Proton is case-insensitive: a file already deployed at a case-variant
        // of this path (e.g. QUI.dll vs qui.dll) would both be visible to the game and
        // load twice. The current mod is later in the load order = higher priority =
        // winner, so drop the stale variant and let this file take its place. Because
        // the conflict index already keys by conflictKey, the prior owner is recorded
        // as a real loser below (no stale setWinner under the displaced variant).
        if (!priorRel.isEmpty() && priorRel != destRel) {
            QFile::remove(gameDir + "/" + priorRel);
            record.remove(priorRel);
        }

        // First Solero owner of this path and something already lives there:
        // it's a genuine pre-existing original (deploy() ran undeploy() first,
        // so any Solero-owned file at this path is already gone). Move it to
        // the backup tree so undeploy can restore it later.
        if (previousOwner.isEmpty() && QFile::exists(dstPath)) {
            QString backupPath = gameDir + "/" + backupDirName() + "/" + destRel;
            // Don't clobber an existing backup (e.g. from an interrupted run);
            // the first one we saw is the true original.
            if (!QFile::exists(backupPath)) {
                QDir().mkpath(QFileInfo(backupPath).path());
                if (!QFile::rename(dstPath, backupPath)) {
                    // Cross-fs rename can fail; fall back to copy + remove.
                    if (QFile::copy(dstPath, backupPath)) {
                        QFile::remove(dstPath);
                    } else {
                        // both backup paths failed. Do not link over the
                        // un-backed-up original - that would destroy a vanilla/CC
                        // file with no restore entry. Count as a failure and skip.
                        qCWarning(lcDeploy) << "backup-move failed for" << destRel
                                            << "(mod" << modId << ") - skipping link "
                                               "to preserve the original";
                        ++failures;
                        continue;
                    }
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
            qCWarning(lcDeploy) << "Deploy failed for" << relPath << "(mod" << modId << ")";
            ++failures;
            continue; // do not record a failed link as deployed
        }

        if (!previousOwner.isEmpty()) {
            conflicts.recordConflict(conflictKey, modId, previousOwner);
        } else {
            conflicts.setWinner(conflictKey, modId);
        }
        record.add(destRel, modId);
        ciOwners.insert(ciKey, destRel);
        qCDebug(lcDeploy) << "linked" << destRel << "(mod" << modId << ")";
    }
    return failures;
}

int DeployEngine::applyWinnerOverrides(Profile& profile,
                                       const QString& gameDir,
                                       const Linker& linker,
                                       DeployRecord& record,
                                       ConflictIndex& conflicts,
                                       QHash<QString, QString>& ciOwners) {
    int failures = 0;
    for (auto it = m_fileOverrides.cbegin(); it != m_fileOverrides.cend(); ++it) {
        const QString relPath = it.key();
        const QString modId   = it.value();
        if (modId.isEmpty()) continue;

        // The forced winner must be an enabled mod in the current load order.
        const ModEntry* entry = profile.modList().findById(modId);
        if (!entry || entry->type != EntryType::Mod || !entry->enabled) {
            qCWarning(lcDeploy) << "Winner override skipped: mod" << modId
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
        const QString srcPath = stagingPathFor(m_stagingRoot, *entry) + "/" + relPath;
        if (!QFile::exists(srcPath)) {
            qCWarning(lcDeploy) << "Winner override skipped: mod" << modId
                                << "does not provide" << relPath;
            continue;
        }

        // Resolve to the canonical on-disk casing (mirrors deployMod).
        const QString destRel = canonicalizeRelPath(gameDir, relPath);

        const QString ciKey  = relPath.toLower();
        const QString priorRel = ciOwners.value(ciKey);
        const QString currentOwner = priorRel.isEmpty() ? QString() : record.ownerOf(priorRel);
        if (currentOwner == modId && priorRel == destRel)
            continue; // already the winner at this exact path - nothing to do

        const QString dstPath = gameDir + "/" + destRel;

        // Drop any stale case-variant the load-order loop deployed (Wine/Proton is
        // case-insensitive, so both would otherwise be visible to the game).
        if (!priorRel.isEmpty() && priorRel != destRel) {
            QFile::remove(gameDir + "/" + priorRel);
            record.remove(priorRel);
        }

        // No Solero owner yet but a file sits at the slot -> it's a pre-existing
        // original; back it up so undeploy can restore it (mirrors deployMod).
        if (currentOwner.isEmpty() && QFile::exists(dstPath)) {
            QString backupPath = gameDir + "/" + backupDirName() + "/" + destRel;
            if (!QFile::exists(backupPath)) {
                QDir().mkpath(QFileInfo(backupPath).path());
                if (!QFile::rename(dstPath, backupPath)) {
                    if (QFile::copy(dstPath, backupPath)) {
                        QFile::remove(dstPath);
                    } else {
                        // (mirror): both backup paths failed - skip linking
                        // rather than clobber the un-backed-up original.
                        qCWarning(lcDeploy) << "backup-move failed for" << destRel
                                            << "(mod" << modId << ") - skipping link "
                                               "to preserve the original";
                        ++failures;
                        continue;
                    }
                }
            }
        }

        QString realSrc = QFileInfo(srcPath).canonicalFilePath();
        if (realSrc.isEmpty()) realSrc = srcPath;
        if (!linker.deploy(realSrc, dstPath)) {
            qCWarning(lcDeploy) << "Winner override failed to link" << relPath << "(mod" << modId << ")";
            ++failures;
            continue;
        }
        // The displaced load-order winner becomes a loser of the forced choice.
        // Key the conflict index by the canonical case-folded path (== ciKey),
        // consistent with deployMod, so case-variants map to one entry.
        if (!currentOwner.isEmpty() && currentOwner != modId)
            conflicts.recordConflict(ciKey, modId, currentOwner);
        else
            conflicts.setWinner(ciKey, modId);
        record.add(destRel, modId);
        ciOwners.insert(ciKey, destRel);
        qCDebug(lcDeploy) << "winner-override" << destRel << "-> mod" << modId;
    }
    return failures;
}

bool DeployEngine::undeploy(const QString& gameDir, const std::function<void(int,int)>& onProgress) {
    const QString normGameDir = QDir::cleanPath(gameDir);
    QString recPath = recordPath(normGameDir);
    DeployRecord record = DeployRecord::loadFromFile(recPath);

    const QString backupRoot = normGameDir + "/" + backupDirName();

    auto paths = record.allPaths();
    int total = paths.size();
    int done = 0;
    int failures = 0; // recorded files we couldn't remove / originals we couldn't restore
    qCInfo(lcDeploy) << "undeploy start:" << total << "recorded files, gameDir" << normGameDir;
    for (const auto& relPath : paths) {
        QString fullPath = normGameDir + "/" + relPath;
        // A recorded path that still exists after we tried to remove it is a real
        // failure (leaves a stale deployed file); count it so we report !ok.
        if (QFile::exists(fullPath) && !QFile::remove(fullPath) && QFile::exists(fullPath)) {
            ++failures;
            qCWarning(lcDeploy) << "undeploy: failed to remove" << fullPath;
        }

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
                if (QFile::copy(backupPath, dstPath)) {
                    QFile::remove(backupPath);
                } else {
                    // an original we can't restore is a failure too.
                    ++failures;
                    qCWarning(lcDeploy) << "backup-restore failed for" << relPath;
                }
            }
        }
        QDir(backupRoot).removeRecursively();
    }

    // Keep the record if anything remained so a later undeploy can retry those
    // paths; only drop it once the game dir is fully cleaned.
    if (failures == 0) QFile::remove(recPath);
    qCInfo(lcDeploy) << "undeploy done:" << (total - failures) << "of" << total
                     << "recorded files removed," << failures << "failure(s)";
    return failures == 0;
}

QString DeployEngine::canonicalizeRelPath(const QString& gameDir, const QString& relPath) {
    const QStringList parts = relPath.split('/', Qt::SkipEmptyParts);
    QString absDir = gameDir;
    QStringList out;
    out.reserve(parts.size());
    for (const QString& want : parts) {
        QHash<QString, QString>& bucket = m_dirCaseIndex[absDir];
        if (!m_dirCaseScanned.contains(absDir)) {
            const QStringList entries = QDir(absDir).entryList(
                QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
            for (const QString& e : entries)
                bucket.insert(e.toLower(), e);
            m_dirCaseScanned.insert(absDir);
        }
        const QString lower = want.toLower();
        QString actual = bucket.value(lower);
        if (actual.isEmpty()) {
            actual = want;
            bucket.insert(lower, actual); // siblings created later reuse this casing
        }
        out.append(actual);
        absDir = absDir + "/" + actual;
    }
    return out.join('/');
}

} // namespace solero
