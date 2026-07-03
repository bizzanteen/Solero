#pragma once
#include "DeployMode.h"
#include "DeployRecord.h"
#include "ConflictIndex.h"
#include "Linker.h"
#include <QString>
#include <QHash>
#include <QSet>
#include <functional>

namespace solero { class Profile; struct ModEntry; }

namespace solero {

struct DeployResult {
    bool success = false;
    QString errorMessage;
    QString warning;
    ConflictIndex conflicts;
    int filesDeployed = 0;
};

class DeployEngine {
public:
    DeployEngine(const QString& gameDir, const QString& stagingRoot);

    DeployResult deploy(Profile& profile, DeployMode mode = DeployMode::HardLink, const std::function<void(int,int)>& onProgress = {});
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
                             DeployRecord& record,
                             ConflictIndex& conflicts,
                             QHash<QString, QString>& ciOwners);

    // Deploys one mod's files. Returns the number of files that FAILED to link.
    // record/conflicts are only updated for files that actually deployed.
    int deployMod(const ModEntry& mod,
                  const QString& gameDir,
                  const Linker& linker,
                  DeployRecord& record,
                  ConflictIndex& conflicts,
                  QHash<QString, QString>& ciOwners);

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
