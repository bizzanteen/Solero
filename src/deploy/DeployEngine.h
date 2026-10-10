#pragma once
#include "DeployMode.h"
#include "DeployRecord.h"
#include "DeployJournal.h"
#include "ConflictIndex.h"
#include "Linker.h"
#include <QString>
#include <QHash>
#include <QSet>
#include <QFileInfo>
#include <functional>

namespace solero { class Profile; struct ModEntry; }

namespace solero {

struct DeployResult {
    bool success = false;
    QString errorMessage;
    QString warning;
    ConflictIndex conflicts;
    int filesDeployed = 0;
    // incremental = false when deploy() fell back to a full teardown+relink.
    bool incremental = false;
    int filesLinked = 0;       // relinked (unlink+hardlink) this deploy
    int filesUnchanged = 0;    // skipped as already-correct (fingerprint or inode)
    int filesAlreadyLinked = 0; // subset of filesUnchanged caught by the inode check
    int filesRemoved = 0;
    // Untracked leftovers from an earlier deploy, cleaned up by this one.
    int filesOrphansSwept = 0;
};

class DeployEngine {
public:
    DeployEngine(const QString& gameDir, const QString& stagingRoot);

    // forceFull bypasses the incremental path and does a full teardown+relink (the
    // "Full Redeploy" escape hatch). deploy() also falls back to full automatically
    // when the on-disk record is missing, legacy (v1), or was written in a different
    // DeployMode.
    DeployResult deploy(Profile& profile, DeployMode mode = DeployMode::HardLink,
                        const std::function<void(int,int)>& onProgress = {},
                        bool forceFull = false);
    bool undeploy(const QString& gameDir, const std::function<void(int,int)>& onProgress = {});

    void setLootEnabled(bool enabled) { m_lootEnabled = enabled; }
    void setUserlistPath(const QString& path) { m_userlistPath = path; }

    // Per-profile Overwrite dir; contents are gameDir-relative and deploy last,
    // above every mod. Empty = no Overwrite layer.
    void setOverwriteDir(const QString& path) { m_overwriteDir = path; }

    // Synthetic deploy-record / conflict-index owner id for the Overwrite layer. It
    // is never a real ModEntry, so it must not be looked up in any modlist.
    static QString overwriteOwnerId() { return QStringLiteral("__overwrite__"); }

    // Per-file conflict rules (see Profile). hidden: modId -> relPaths skipped on
    // deploy; overrides: relPath -> modId forced to win that path. deploy() seeds
    // these from the Profile automatically; this setter exists for explicit use.
    void setFileRules(const QHash<QString, QSet<QString>>& hidden,
                      const QHash<QString, QString>& overrides) {
        m_hiddenFiles = hidden; m_fileOverrides = overrides;
    }

    static QString recordPath(const QString& gameDir);
    static QString conflictIndexPath(const QString& profileDir);
    // Translate a staged path into the location read by the selected game.
    static QString routeRelativePath(const QString& gameType, const QString& relPath);

private:
    QString m_gameDir;
    QString m_stagingRoot;
    bool    m_lootEnabled = true;
    QString m_userlistPath;
    QHash<QString, QSet<QString>> m_hiddenFiles;   // modId  -> hidden relPaths
    QHash<QString, QString>       m_fileOverrides;  // relPath -> forced winner modId
    QString m_overwriteDir;                          // per-profile Overwrite dir; empty = no layer

    // ciKey (lowercased relPath) -> winning modId, in deploy order (last wins).
    // Lets deployMod skip losers, so a conflicted path is linked once, not once
    // per provider. Rebuilt at the top of every deploy().
    QHash<QString, QString> m_winners;

    // Build m_winners for a deploy: the single load-order winner of every path.
    // Forced per-file overrides are applied afterwards by applyWinnerOverrides, so
    // they are intentionally NOT folded in here (forcing a non-provider would make
    // every real provider skip and drop the file entirely).
    QHash<QString, QString> computeWinners(const Profile& profile) const;

    // Re-link forced per-path winners after the normal mod loop. See deploy().
    // Returns the number of forced winners that FAILED to link (a mod present and
    // enabled that staged the path but couldn't be deployed) - these are added to
    // the deploy failure count so a silently-wrong conflict winner is reported.
    int applyWinnerOverrides(Profile& profile,
                             const QString& gameDir,
                             const Linker& linker,
                             const DeployRecord& prevRecord,
                             DeployRecord& record,
                             ConflictIndex& conflicts,
                             QHash<QString, QString>& ciOwners);

    // Deploys one mod's files; returns the count that failed to link. Skips the
    // link when prevRecord shows this mod already owning the path with an unchanged
    // fingerprint. modRootOverride replaces the staging lookup (the Overwrite layer
    // rides through here that way).
    int deployMod(const ModEntry& mod,
                  const QString& gameDir,
                  const Linker& linker,
                  const DeployRecord& prevRecord,
                  DeployRecord& record,
                  ConflictIndex& conflicts,
                  QHash<QString, QString>& ciOwners,
                  const QString& modRootOverride = {});

    // Delete every prevRecord path absent from newRecord, restore any backed-up
    // original, prune emptied dirs. A path it can't remove goes back into newRecord
    // so a later deploy retries. No-op when prevRecord is empty.
    int removeStalePaths(const DeployRecord& prevRecord, DeployRecord& newRecord,
                         const QString& gameDir, int& failures);

    // Remove game-dir files sharing an inode with content staged by a mod this
    // profile doesn't deploy, and absent from the new record. Lossless (the bytes
    // stay in staging). HardLink mode only; a copy looks like a vanilla file.
    int sweepUndeployedModOrphans(const Profile& profile, const DeployRecord& newRecord,
                                  const Linker& linker, const QString& gameDir);

    // Undeploy safety net: drop game-dir hardlinks to staged content the record
    // never tracked. Lossless (the bytes stay in staging). Never touches
    // single-link files, the backup tree, or staged paths inside the game dir.
    int sweepStagingOrphans(const QString& gameDir, const QString& backupRoot);

    // Source-file fingerprint (size + mtime ms) used for the incremental skip test
    // and stored in the record. Follows symlinks so it reflects real content.
    DeployRecord::Fingerprint fingerprintOf(const QFileInfo& srcInfo) const;

    // Every record.add() during a deploy goes through here, or an interrupted
    // deploy leaves the file untracked. size < 0 = no fingerprint.
    void recordFile(DeployRecord& record, const QString& relPath, const QString& modId,
                    qint64 size = -1, qint64 mtimeMs = -1);

    DeployJournal m_journal;

    // Per-deploy link/skip tallies (reset at the top of deploy()).
    int m_linkCount = 0;      // relinked (actual unlink+hardlink)
    int m_skipCount = 0;      // skipped via the record fingerprint (unchanged source)
    int m_alreadyCorrect = 0; // skipped via the inode short-circuit (already linked)

    // Adopt the game dir's existing directory casing per component, so all mods
    // funnel into one casing (Wine is case-insensitive, the Linux dir isn't). New
    // components keep their staged casing.
    QString canonicalizeRelPath(const QString& gameDir, const QString& relPath);

    // Per-deploy case-folding caches for canonicalizeRelPath(). Keyed by
    // absolute dir -> (lowercased child name -> actual on-disk name). Cleared at
    // the start of each deploy(). m_dirCaseScanned tracks which dirs are read.
    mutable QHash<QString, QHash<QString, QString>> m_dirCaseIndex;
    mutable QSet<QString> m_dirCaseScanned;

    // Backup dir living inside the game dir; holds pre-existing (non-Solero)
    // originals that mods were deployed over, so undeploy can restore them.
    static QString backupDirName() { return ".solero-backup"; }
};

} // namespace solero
