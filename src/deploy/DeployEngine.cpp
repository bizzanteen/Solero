#include "DeployEngine.h"
#include "Linker.h"
#include "ProfileArtifacts.h"
#include "loot/LootSorter.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include "core/StagingFolder.h"
#include "core/FileUtil.h"
#include "core/Log.h"
#include "bethini/IniFile.h"
#include <QDirIterator>
#include <QFile>
#include <QDir>
#include <QSet>
#include <QPair>
#include <QFileInfo>
#include <QDateTime>
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

DeployResult DeployEngine::deploy(Profile& profile, DeployMode mode,
                                  const std::function<void(int,int)>& onProgress,
                                  bool forceFull) {
    DeployResult result;
    m_linkCount = 0;
    m_skipCount = 0;
    m_alreadyCorrect = 0;

    // Full teardown when there's nothing safe to diff against: no record, a v1 one
    // (no fingerprints), a different DeployMode, or a forced Full Redeploy.
    const QString recPath = recordPath(m_gameDir);
    DeployRecord prevRecord = DeployRecord::loadFromFile(recPath);
    const bool fullRedeploy = forceFull
        || prevRecord.count() == 0
        || prevRecord.version() < 2
        || prevRecord.deployMode() != int(mode);
    if (fullRedeploy) {
        // Full teardown restores originals and empties the game dir, so there's
        // nothing to skip against or sweep afterwards.
        undeploy(m_gameDir);
        prevRecord.clear();
    }
    result.incremental = !fullRedeploy;

    // Seed per-file conflict rules from the profile (hidden files + forced winners).
    m_hiddenFiles   = profile.hiddenFiles();
    m_fileOverrides = profile.fileOverrides();

    Linker linker(mode);
    DeployRecord record;
    record.setDeployMode(int(mode)); // persisted with the record at the end (see below)
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

    // Resolve the single load-order winner of every path up front so deployMod can
    // skip loser candidates outright (a conflicted path is then linked once, by its
    // winner, not relinked once per providing mod on every deploy).
    m_winners = computeWinners(profile);

    int total = 0;
    for (const auto& entry : profile.modList())
        if (entry.type == EntryType::Mod && entry.enabled) ++total;
    qCInfo(lcDeploy) << "deploy start:" << total << "enabled mods, mode" << int(mode)
                     << ", staging" << m_stagingRoot;
    // The Overwrite layer (if present and non-empty) deploys as one more step.
    const bool willDeployOverwrite =
        !m_overwriteDir.isEmpty() && QDir(m_overwriteDir).exists();
    if (willDeployOverwrite) ++total;
    total += 1; // finalize/LOOT step
    int done = 0;
    if (onProgress) onProgress(0, total);

    // The record file is NOT rewritten here. It still describes the previous
    // deployment, whose files aren't unlinked until removeStalePaths() below; a
    // deploy that stopped in between would leave them untracked for good. New links
    // go to the journal instead. See DeployJournal.
    bool recordSaveFailed = false;
    if (!m_journal.begin(recPath))
        qCWarning(lcDeploy) << "could not open the deploy journal at"
                            << DeployJournal::pathFor(recPath)
                            << "- an interrupted deploy may leave untracked files";
    auto flushRecord = [&]() { m_journal.flush(); };

    int failures = 0;
    // Deploy every enabled mod in list order.
    for (const auto& entry : profile.modList()) {
        if (entry.type != EntryType::Mod) continue;
        if (!entry.enabled) continue;
        // Flush only after a mod that linked something; the unchanged majority
        // was already persisted by the deploy that first linked it.
        const int linksBefore = m_linkCount;
        failures += deployMod(entry, m_gameDir, linker, prevRecord, record, conflicts, ciOwners);
        if (m_linkCount != linksBefore) flushRecord();
        ++done;
        if (onProgress) onProgress(done, total);
    }
    // The Community Shaders shader cache now lives inside the CS mod's own staging
    // folder, so it deploys as ordinary CS content in the load-order loop above - no
    // separate synthetic layer.

    // Force per-path winners (Vortex/MO2 per-file conflict choice). Runs after the
    // normal load-order loop so a chosen mod wins regardless of its priority.
    failures += applyWinnerOverrides(profile, m_gameDir, linker, prevRecord, record, conflicts, ciOwners);
    flushRecord();

    // Overwrite goes last so runtime-captured files win every conflict and come
    // back each deploy instead of being regenerated. Modelled as a synthetic
    // top-priority mod so it reuses deployMod's machinery.
    if (willDeployOverwrite) {
        ModEntry ow;
        ow.type    = EntryType::Mod;
        ow.id      = overwriteOwnerId();
        ow.name    = QStringLiteral("Overwrite");
        ow.enabled = true;
        failures += deployMod(ow, m_gameDir, linker, prevRecord, record,
                              conflicts, ciOwners, m_overwriteDir);
        flushRecord();
        ++done;
        if (onProgress) onProgress(done, total);
    }

    // Sort plugins with LOOT (if enabled). The mod files must already be deployed
    // above so LOOT can read the plugin headers. A locked load order skips the
    // auto-sort entirely and deploys the current manual order as-is.
    QString lootSortError; // non-empty => append to the deploy warning below
    if (m_lootEnabled && !profile.pluginList().loadOrderLocked()) {
        // libloot's LoadCurrentLoadOrderState() reads Plugins.txt off disk to learn
        // which plugins are active, so push this profile's load order out FIRST -
        // otherwise the sort is informed by whatever the previous deploy wrote.
        writeProfileArtifacts(profile, m_gameDir);
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

    // Record generated artifacts under a sentinel owner when they land inside
    // gameDir, so undeploy removes them. Ones written into the prefix are outside
    // gameDir and deliberately not recorded.
    const QString normGameDirForRec = QDir::cleanPath(m_gameDir);
    auto recordIfInGameDir = [&](const QString& target) {
        const QString norm = QDir::cleanPath(target);
        if (norm.startsWith(normGameDirForRec + "/")) {
            const QString rel = norm.mid(normGameDirForRec.length() + 1);
            recordFile(record, rel, kGeneratedArtifactOwner);
        }
    };

    // Write the generated artifacts through the same helper the launch path uses,
    // so a deploy and a plain Play produce byte-identical files. This is the write
    // that counts - the pre-sort one above only exists to inform libloot, and the
    // sort/applyPins step reorders the list after it.
    const ArtifactWriteResult artifacts = writeProfileArtifacts(profile, m_gameDir);
    // inPlace as well as written: an artifact the writer deliberately preserved is
    // current, and leaving it out of the record would let the stale sweep below
    // delete it.
    for (const QString& path : artifacts.written) recordIfInGameDir(path);
    for (const QString& path : artifacts.inPlace) recordIfInGameDir(path);
    if (!artifacts.ok)
        qCWarning(lcDeploy) << "artifact write problems:" << artifacts.errors;

    // JContainers crashes at load without JCData/Domains, and it doesn't ship the
    // empty dir - MO2's VFS provides it virtually. (ryobg/JContainers#38.)
    {
        const QString jcData = m_gameDir + "/Data/SKSE/Plugins/JCData";
        if (QDir(jcData).exists()) QDir().mkpath(jcData + "/Domains");
    }

    // Stale sweep: drop what the previous deployment had and this one doesn't, and
    // restore the originals they covered. Must run after the artifacts above are
    // recorded so they aren't mistaken for stale.
    //
    // First work out what it ought to remove. "0 removed" right after disabling
    // mods used to pass silently, and that's the orphaning bug's signature.
    QSet<QString> deployedOwners{overwriteOwnerId(), kGeneratedArtifactOwner};
    for (const auto& e : profile.modList())
        if (e.type == EntryType::Mod && e.enabled) deployedOwners.insert(e.id);
    int expectedStale = 0;
    QSet<QString> retiredOwners;
    for (const QString& rel : prevRecord.allPaths()) {
        const QString owner = prevRecord.ownerOf(rel);
        if (deployedOwners.contains(owner)) continue;
        ++expectedStale;
        retiredOwners.insert(owner);
    }

    result.filesRemoved = removeStalePaths(prevRecord, record, m_gameDir, failures);

    if (expectedStale > 0) {
        qCInfo(lcDeploy) << "stale sweep: expected at least" << expectedStale
                         << "file(s) from" << retiredOwners.size()
                         << "no-longer-deployed mod(s), removed" << result.filesRemoved;
        if (result.filesRemoved < expectedStale)
            qCWarning(lcDeploy) << "stale sweep removed fewer files than expected ("
                                << result.filesRemoved << "of" << expectedStale
                                << ") - the game dir may still hold files from mods "
                                   "that are no longer deployed";
    }

    // Orphans an earlier deploy left behind, which the record can no longer see.
    result.filesOrphansSwept = sweepUndeployedModOrphans(profile, record, linker, m_gameDir);
    if (result.filesOrphansSwept > 0)
        qCWarning(lcDeploy) << "removed" << result.filesOrphansSwept
                            << "untracked file(s) left in the game dir by an earlier "
                               "deploy (their mods are no longer deployed)";

    // Written once, here: every removal above has happened, so the record now
    // matches the game dir. The journal goes only after this succeeds.
    if (!record.saveToFile(recPath)) {
        recordSaveFailed = true;
        qCWarning(lcDeploy) << "failed to persist deploy record to" << recPath;
    } else {
        m_journal.discard();
    }
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
    result.filesLinked = m_linkCount;
    result.filesUnchanged = m_skipCount + m_alreadyCorrect;
    result.filesAlreadyLinked = m_alreadyCorrect;
    qCInfo(lcDeploy).nospace()
        << "deploy done (" << (result.incremental ? "incremental" : "full") << "): "
        << m_linkCount << " relinked, " << (m_skipCount + m_alreadyCorrect)
        << " unchanged (" << m_alreadyCorrect << " already-linked), "
        << result.filesRemoved << " removed, " << result.filesOrphansSwept
        << " orphans swept, " << conflictCount << " conflicts, "
        << failures << " failures";
    return result;
}

QHash<QString, QString> DeployEngine::computeWinners(const Profile& profile) const {
    QHash<QString, QString> winner; // ciKey (lowercased relPath) -> owning modId
    auto scan = [&](const QString& modId, const QString& modRoot) {
        if (modRoot.isEmpty() || !QDir(modRoot).exists()) return;
        const auto hiddenIt = m_hiddenFiles.constFind(modId);
        const bool hasHidden = hiddenIt != m_hiddenFiles.constEnd();
        QDirIterator it(modRoot, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QString srcPath = it.next();
            const QString relPath = srcPath.mid(modRoot.length() + 1);
            const QString fn = QFileInfo(srcPath).fileName();
            // Same eligibility as deployMod: never deploy per-mod metadata, and
            // respect per-file HIDE, so the winner map matches what actually links.
            if (fn.startsWith(".solero")
                || fn.compare("fomod-choices.json", Qt::CaseInsensitive) == 0)
                continue;
            if (hasHidden && hiddenIt.value().contains(relPath))
                continue;
            winner.insert(relPath.toLower(), modId); // later scan (higher priority) wins
        }
    };
    // Order mirrors deploy(): mods in list order (the CS shader cache rides inside
    // the CS mod), then the Overwrite layer - each scanned after the previous so
    // last-wins is preserved.
    for (const auto& e : profile.modList())
        if (e.type == EntryType::Mod && e.enabled)
            scan(e.id, stagingPathFor(m_stagingRoot, e));
    if (!m_overwriteDir.isEmpty() && QDir(m_overwriteDir).exists())
        scan(overwriteOwnerId(), m_overwriteDir);
    return winner;
}

int DeployEngine::deployMod(const ModEntry& mod,
                             const QString& gameDir,
                             const Linker& linker,
                             const DeployRecord& prevRecord,
                             DeployRecord& record,
                             ConflictIndex& conflicts,
                             QHash<QString, QString>& ciOwners,
                             const QString& modRootOverride) {
    // The id remains the deploy-record owner key; the on-disk folder is resolved
    // via the (name-based) staging folder, unless the caller overrides the source
    // root (the Overwrite layer points it at the Overwrite dir).
    const QString modId = mod.id;
    QString modRoot = modRootOverride.isEmpty()
        ? stagingPathFor(m_stagingRoot, mod) : modRootOverride;
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

        // Funnel every mod into one on-disk casing per directory, and let the
        // backup check below find a vanilla file of differing case.
        const QString destRel = canonicalizeRelPath(gameDir, relPath);
        QString dstPath = gameDir + "/" + destRel;

        const QString ciKey = relPath.toLower();

        // Another mod wins this path. Record the conflict but don't link, so a
        // conflicted path is touched once per deploy rather than once per provider.
        {
            const QString winnerId = m_winners.value(ciKey);
            if (!winnerId.isEmpty() && winnerId != modId) {
                conflicts.recordConflict(ciKey, winnerId, modId);
                continue;
            }
        }

        const QString priorRel = ciOwners.value(ciKey);   // earlier-deployed path at this CI path, if any
        QString previousOwner = priorRel.isEmpty() ? QString() : record.ownerOf(priorRel);

        // Case-folded, so QUI.dll and qui.dll map to one conflict entry rather
        // than two, one of which would show the loser as winning.
        const QString conflictKey = ciKey;

        // Source fingerprint (size + mtime) for the incremental skip test and for
        // recording. it.fileInfo() reuses the walk's cached stat; size()/lastModified()
        // follow symlinks so this reflects the real staged content.
        const QFileInfo srcInfo = it.fileInfo();
        const DeployRecord::Fingerprint srcFp = fingerprintOf(srcInfo);

        // First provider this pass, same owner as last time, source unchanged, file
        // still there: reuse it. Still recorded, so the new record stays complete.
        // A non-empty priorRel means another mod already wrote here, so never skip -
        // the link is what establishes the winner.
        if (priorRel.isEmpty() && prevRecord.ownerOf(destRel) == modId
            && QFile::exists(dstPath)) {
            const auto pfp = prevRecord.fingerprintOf(destRel);
            if (pfp.valid() && pfp.size == srcFp.size && pfp.mtimeMs == srcFp.mtimeMs) {
                conflicts.setWinner(conflictKey, modId);
                recordFile(record, destRel, modId, srcFp.size, srcFp.mtimeMs);
                ciOwners.insert(ciKey, destRel);
                ++m_skipCount;
                continue;
            }
        }

        // Both case-variants would be visible to the game and load twice. This mod
        // is later in the order, so it wins; drop the stale variant.
        if (!priorRel.isEmpty() && priorRel != destRel) {
            QFile::remove(gameDir + "/" + priorRel);
            record.remove(priorRel);
        }

        // Something already here and no Solero owner: a real pre-existing original,
        // so back it up for undeploy. The prevRecord guard stops us capturing our own
        // link from the previous deployment and resurrecting it on restore.
        if (previousOwner.isEmpty() && !prevRecord.contains(destRel)
            && QFile::exists(dstPath)) {
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

        // Link the real file, not the symlink: deploying the link itself makes
        // skse64_loader.exe run from the source dir and launch the wrong game.
        // canonicalFilePath is a syscall, so only pay it for actual symlinks.
        QString realSrc = srcPath;
        if (srcInfo.isSymLink()) {
            const QString canon = srcInfo.canonicalFilePath();
            if (!canon.isEmpty()) realSrc = canon;
        }

        // Ground-truth short-circuit: the destination is already the correct hardlink
        // to this source (same device+inode), so its bytes and mtime already match -
        // skip the unlink+relink even when the record fingerprint didn't line up (e.g.
        // btrfs mtime granularity). Still record it so the new record stays complete.
        if (linker.alreadyDeployed(realSrc, dstPath)) {
            if (!previousOwner.isEmpty()) conflicts.recordConflict(conflictKey, modId, previousOwner);
            else                          conflicts.setWinner(conflictKey, modId);
            recordFile(record, destRel, modId, srcFp.size, srcFp.mtimeMs);
            ciOwners.insert(ciKey, destRel);
            ++m_alreadyCorrect;
            continue;
        }

        if (!linker.deploy(realSrc, dstPath)) {
            qCWarning(lcDeploy) << "Deploy failed for" << relPath << "(mod" << modId << ")";
            ++failures;
            continue; // do not record a failed link as deployed
        }
        ++m_linkCount;

        if (!previousOwner.isEmpty()) {
            conflicts.recordConflict(conflictKey, modId, previousOwner);
        } else {
            conflicts.setWinner(conflictKey, modId);
        }
        recordFile(record, destRel, modId, srcFp.size, srcFp.mtimeMs);
        ciOwners.insert(ciKey, destRel);
        qCDebug(lcDeploy) << "linked" << destRel << "(mod" << modId << ")";
    }
    return failures;
}

int DeployEngine::applyWinnerOverrides(Profile& profile,
                                       const QString& gameDir,
                                       const Linker& linker,
                                       const DeployRecord& prevRecord,
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
        // original; back it up so undeploy can restore it (mirrors deployMod). The
        // prevRecord guard prevents capturing our own prior-deploy link (incremental).
        if (currentOwner.isEmpty() && !prevRecord.contains(destRel)
            && QFile::exists(dstPath)) {
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

        // only canonicalize when the source is actually a symlink.
        const QFileInfo srcInfo(srcPath);
        const DeployRecord::Fingerprint srcFp = fingerprintOf(srcInfo);
        QString realSrc = srcPath;
        if (srcInfo.isSymLink()) {
            const QString canon = srcInfo.canonicalFilePath();
            if (!canon.isEmpty()) realSrc = canon;
        }
        // Already the correct hardlink from a prior deploy? Skip the relink (see deployMod).
        if (linker.alreadyDeployed(realSrc, dstPath)) {
            if (!currentOwner.isEmpty() && currentOwner != modId)
                conflicts.recordConflict(ciKey, modId, currentOwner);
            else
                conflicts.setWinner(ciKey, modId);
            recordFile(record, destRel, modId, srcFp.size, srcFp.mtimeMs);
            ciOwners.insert(ciKey, destRel);
            ++m_alreadyCorrect;
            continue;
        }
        if (!linker.deploy(realSrc, dstPath)) {
            qCWarning(lcDeploy) << "Winner override failed to link" << relPath << "(mod" << modId << ")";
            ++failures;
            continue;
        }
        ++m_linkCount;
        // The displaced load-order winner becomes a loser of the forced choice.
        // Key the conflict index by the canonical case-folded path (== ciKey),
        // consistent with deployMod, so case-variants map to one entry.
        if (!currentOwner.isEmpty() && currentOwner != modId)
            conflicts.recordConflict(ciKey, modId, currentOwner);
        else
            conflicts.setWinner(ciKey, modId);
        recordFile(record, destRel, modId, srcFp.size, srcFp.mtimeMs);
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

    // Files Solero hardlinked in but never recorded. The loop above can't see them,
    // so they're found structurally instead - no record needed, however they arose.
    const int swept = sweepStagingOrphans(normGameDir, backupRoot);
    if (swept > 0)
        qCInfo(lcDeploy) << "undeploy: swept" << swept << "untracked staging orphan(s)";

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

    // Keep both if anything remained, so a later undeploy can retry those paths.
    if (failures == 0) {
        QFile::remove(recPath);
        QFile::remove(DeployJournal::pathFor(recPath));
    }
    qCInfo(lcDeploy) << "undeploy done:" << (total - failures) << "of" << total
                     << "recorded files removed," << failures << "failure(s)";
    return failures == 0;
}

int DeployEngine::sweepStagingOrphans(const QString& gameDir, const QString& backupRoot) {
    if (m_stagingRoot.isEmpty() || !QDir(m_stagingRoot).exists()) return 0;
    const QString normGameDir = QDir::cleanPath(gameDir);

    // 1. Every (dev,inode) reachable from Solero's own roots: staging plus the
    //    Overwrite dir, whose files a play session can create after the record was
    //    taken. Follow symlinks, but skip anything resolving into the game dir - a
    //    vanilla file is never a removable orphan.
    QSet<QPair<quint64, quint64>> stagedInodes;
    QStringList ownedRoots{m_stagingRoot};
    if (!m_overwriteDir.isEmpty() && QDir(m_overwriteDir).exists())
        ownedRoots << m_overwriteDir;
    for (const QString& root : ownedRoots) {
        QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isSymLink()) {
                const QString canon = fi.canonicalFilePath();
                if (canon.isEmpty()
                    || QDir::cleanPath(canon).startsWith(normGameDir + "/"))
                    continue;
            }
            struct stat st;
            if (::stat(QFile::encodeName(it.filePath()).constData(), &st) == 0)
                stagedInodes.insert({quint64(st.st_dev), quint64(st.st_ino)});
        }
    }
    if (stagedInodes.isEmpty()) return 0;

    // 2. Walk the game dir; collect every file that is a hardlink (nlink >= 2 - a
    //    vanilla Steam file is single-link and is never touched) whose inode is a
    //    staged inode. Collect first, remove after, so we don't mutate the tree
    //    mid-walk. Never descend the backup tree.
    QStringList orphans;
    {
        QDirIterator it(normGameDir, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            if (QDir::cleanPath(p).startsWith(backupRoot)) continue;
            struct stat st;
            if (::stat(QFile::encodeName(p).constData(), &st) != 0) continue;
            if (st.st_nlink < 2) continue; // single-link => vanilla/original, keep
            if (stagedInodes.contains({quint64(st.st_dev), quint64(st.st_ino)}))
                orphans << p;
        }
    }

    int removed = 0;
    for (const QString& p : orphans) {
        if (!QFile::remove(p)) continue;
        ++removed;
        // Prune emptied parent dirs, never the backup tree.
        QDir dir(QFileInfo(p).path());
        while (QDir::cleanPath(dir.path()) != normGameDir
               && !QDir::cleanPath(dir.path()).startsWith(backupRoot)
               && dir.exists() && dir.isEmpty()) {
            const QString dirPath = dir.path();
            const QString parent = QFileInfo(dirPath).path();
            QDir(parent).rmdir(QFileInfo(dirPath).fileName());
            dir = QDir(parent);
        }
    }
    return removed;
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

DeployRecord::Fingerprint DeployEngine::fingerprintOf(const QFileInfo& srcInfo) const {
    return DeployRecord::Fingerprint{ srcInfo.size(),
                                      srcInfo.lastModified().toMSecsSinceEpoch() };
}

void DeployEngine::recordFile(DeployRecord& record, const QString& relPath,
                              const QString& modId, qint64 size, qint64 mtimeMs) {
    record.add(relPath, modId, size, mtimeMs);
    m_journal.append(relPath, modId, size, mtimeMs);
}

int DeployEngine::sweepUndeployedModOrphans(const Profile& profile,
                                            const DeployRecord& newRecord,
                                            const Linker& linker,
                                            const QString& gameDir) {
    if (m_stagingRoot.isEmpty() || !QDir(m_stagingRoot).exists()) return 0;
    const QString normGameDir = QDir::cleanPath(gameDir);
    const QString backupRoot  = normGameDir + "/" + backupDirName();

    // Anything else under the staging root is disabled, gone from the profile, or
    // a retired variant, and has no business having files in the game dir.
    QSet<QString> deployedRoots;
    for (const auto& e : profile.modList())
        if (e.type == EntryType::Mod && e.enabled)
            deployedRoots.insert(QDir::cleanPath(stagingPathFor(m_stagingRoot, e)));

    QStringList orphans;
    const QStringList folders = QDir(m_stagingRoot).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QString& folder : folders) {
        const QString root = QDir::cleanPath(m_stagingRoot + "/" + folder);
        if (deployedRoots.contains(root)) continue;

        QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QString srcPath = it.next();
            const QString fn = QFileInfo(srcPath).fileName();
            if (fn.startsWith(".solero")) continue;   // per-mod metadata, never deployed

            // A symlink-staged mod can point back into the game dir; following it
            // would make a vanilla file look like mod content.
            const QFileInfo srcInfo = it.fileInfo();
            QString realSrc = srcPath;
            if (srcInfo.isSymLink()) {
                const QString canon = srcInfo.canonicalFilePath();
                if (canon.isEmpty()
                    || QDir::cleanPath(canon).startsWith(normGameDir + "/")) continue;
                realSrc = canon;
            }

            // A single-link staged file isn't deployed anywhere, so skip it before
            // paying for the case resolution below. Also no-ops Copy mode.
            struct stat ss;
            if (::stat(QFile::encodeName(realSrc).constData(), &ss) != 0) continue;
            if (ss.st_nlink < 2) continue;

            const QString destRel = canonicalizeRelPath(normGameDir,
                                                        srcPath.mid(root.length() + 1));
            if (newRecord.contains(destRel)) continue;  // this deploy owns it
            const QString dstPath = normGameDir + "/" + destRel;
            if (QDir::cleanPath(dstPath).startsWith(backupRoot)) continue;
            // Same inode is the only safe test - a copy looks like a vanilla file.
            if (linker.alreadyDeployed(realSrc, dstPath)) orphans << dstPath;
        }
    }

    int removed = 0;
    for (const QString& p : orphans) {
        if (!QFile::remove(p)) {
            qCWarning(lcDeploy) << "failed to remove orphaned file" << p;
            continue;
        }
        ++removed;
        qCDebug(lcDeploy) << "swept orphan" << p;
        // Prune now-empty parent dirs up to (not including) the game dir; never the
        // backup tree.
        QDir dir(QFileInfo(p).path());
        while (QDir::cleanPath(dir.path()) != normGameDir
               && !QDir::cleanPath(dir.path()).startsWith(backupRoot)
               && dir.exists() && dir.isEmpty()) {
            const QString dirPath = dir.path();
            const QString parent = QFileInfo(dirPath).path();
            QDir(parent).rmdir(QFileInfo(dirPath).fileName());
            dir = QDir(parent);
        }
    }
    return removed;
}

int DeployEngine::removeStalePaths(const DeployRecord& prevRecord, DeployRecord& newRecord,
                                   const QString& gameDir, int& failures) {
    const QString normGameDir = QDir::cleanPath(gameDir);
    const QString backupRoot = normGameDir + "/" + backupDirName();
    int removed = 0;
    for (const QString& relPath : prevRecord.allPaths()) {
        if (newRecord.contains(relPath)) continue; // still deployed this pass
        const QString fullPath = normGameDir + "/" + relPath;
        bool ok = true;

        // Remove the stale deployed file. A recorded path that still exists after we
        // tried to remove it is a real failure (mirrors undeploy's).
        if (QFile::exists(fullPath) && !QFile::remove(fullPath) && QFile::exists(fullPath)) {
            ok = false;
            ++failures;
            qCWarning(lcDeploy) << "incremental: failed to remove stale" << fullPath;
        }

        // Restore a backed-up pre-existing original into the freed slot, if any.
        // (The path is stale => no mod covers it now => its original should return.)
        const QString backupPath = backupRoot + "/" + relPath;
        if (ok && QFile::exists(backupPath)) {
            QDir().mkpath(QFileInfo(fullPath).path());
            QFile::remove(fullPath);
            if (!QFile::rename(backupPath, fullPath)) {
                if (QFile::copy(backupPath, fullPath)) {
                    QFile::remove(backupPath);
                } else {
                    ok = false;
                    ++failures;
                    qCWarning(lcDeploy) << "incremental: failed to restore original" << relPath;
                }
            }
        }

        if (!ok) {
            // Keep the path tracked so a later deploy retries it (never orphan).
            newRecord.add(relPath, prevRecord.ownerOf(relPath));
            continue;
        }
        ++removed;

        // Prune now-empty parent dirs up to (but not including) the game dir; never
        // descend into / prune the backup tree.
        QDir dir(QFileInfo(fullPath).path());
        while (QDir::cleanPath(dir.path()) != normGameDir
               && !QDir::cleanPath(dir.path()).startsWith(backupRoot)
               && dir.exists() && dir.isEmpty()) {
            const QString dirPath = dir.path();
            const QString parent = QFileInfo(dirPath).path();
            QDir(parent).rmdir(QFileInfo(dirPath).fileName());
            dir = QDir(parent);
        }
    }
    if (removed > 0 || failures > 0)
        qCDebug(lcDeploy) << "incremental sweep removed" << removed << "stale path(s)";
    return removed;
}

} // namespace solero
