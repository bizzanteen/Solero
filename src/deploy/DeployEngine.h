#pragma once
#include "DeployMode.h"
#include "DeployRecord.h"
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
    // incremental-deploy stats. incremental = false when deploy() fell back
    // to a full teardown+relink (first deploy, legacy/mode-mismatched record, or a
    // forced Full Redeploy). filesLinked = actual link operations performed;
    // filesUnchanged = files skipped because they were already deployed unchanged;
    // filesRemoved = stale files pruned by the incremental sweep.
    bool incremental = false;
    int filesLinked = 0;
    int filesUnchanged = 0;
    int filesRemoved = 0;
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

    // Per-file conflict rules (see Profile). hidden: modId -> relPaths skipped on
    // deploy; overrides: relPath -> modId forced to win that path. deploy() seeds
    // these from the Profile automatically; this setter exists for explicit use.
    void setFileRules(const QHash<QString, QSet<QString>>& hidden,
                      const QHash<QString, QString>& overrides) {
        m_hiddenFiles = hidden; m_fileOverrides = overrides;
    }

    static QString recordPath(const QString& gameDir);
    static QString conflictIndexPath(const QString& profileDir);

private:
    QString m_gameDir;
    QString m_stagingRoot;
    bool    m_lootEnabled = true;
    QString m_userlistPath;
    QHash<QString, QSet<QString>> m_hiddenFiles;   // modId  -> hidden relPaths
    QHash<QString, QString>       m_fileOverrides;  // relPath -> forced winner modId

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

    // Deploys one mod's files. Returns the number of files that FAILED to link.
    // record/conflicts are only updated for files that actually deployed.
    // prevRecord is the previous on-disk deployment (empty in full-redeploy mode);
    // deployMod skips the link when prevRecord shows this exact path already
    // deployed by this mod with an unchanged source fingerprint. Skips and
    // links are tallied into m_linkCount / m_skipCount.
    int deployMod(const ModEntry& mod,
                  const QString& gameDir,
                  const Linker& linker,
                  const DeployRecord& prevRecord,
                  DeployRecord& record,
                  ConflictIndex& conflicts,
                  QHash<QString, QString>& ciOwners);

    // Remove every path recorded in prevRecord that is absent from newRecord (the
    // incremental stale sweep): delete the file, restore its backed-up original if
    // one exists, and prune emptied dirs. Returns the count removed; adds any path
    // it could not remove back into newRecord (so a later deploy retries) and bumps
    // *failures. No-op when prevRecord is empty (full redeploy).
    int removeStalePaths(const DeployRecord& prevRecord, DeployRecord& newRecord,
                         const QString& gameDir, int& failures);

    // Source-file fingerprint (size + mtime ms) used for the incremental skip test
    // and stored in the record. Follows symlinks so it reflects real content.
    DeployRecord::Fingerprint fingerprintOf(const QFileInfo& srcInfo) const;

    // Per-deploy link/skip tallies (reset at the top of deploy()).
    int m_linkCount = 0;
    int m_skipCount = 0;

    // Resolve a staged relPath against the live game dir, adopting existing
    // on-disk directory casing component-by-component (Wine/Proton is
    // case-insensitive but the Linux game dir is case-sensitive, so all mods
    // must funnel into a single casing per directory). New components keep their
    // staged casing. Uses the per-deploy caches below; cleared in deploy().
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
