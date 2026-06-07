#pragma once
#include "DeployMode.h"
#include "DeployRecord.h"
#include "ConflictIndex.h"
#include "Linker.h"
#include <QString>
#include <QHash>
#include <QSet>
#include <functional>

namespace solero { class Profile; }

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

    // Override the Overwrite source dir (defaults to dataRoot/overwrite). The
    // Overwrite layer deploys last, above every mod. Exposed mainly for tests.
    void setOverwriteDir(const QString& path) { m_overwriteDir = path; }

    // Synthetic owner id for files deployed from the Overwrite folder. Used as a
    // DeployRecord/ConflictIndex key only - it is never a real ModEntry, so it
    // must not be looked up in any modlist.
    static QString overwriteOwnerId() { return QStringLiteral("__overwrite__"); }

    // Incrementally link the Overwrite folder's files into gameDir/Data and append
    // them to the ON-DISK deploy record (load -> link -> record.add -> save), so
    // files captured into Overwrite after a play session reach the game without a
    // full redeploy. Idempotent for already-linked files. overwriteDir defaults to
    // dataRoot/overwrite. No-op if the Overwrite dir is empty/absent.
    static void deployOverwriteIncremental(const QString& gameDir, DeployMode mode,
                                           const QString& overwriteDir = {});

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
    QString m_overwriteDir;                         // override; empty = dataRoot/overwrite

    // Re-link forced per-path winners after the normal mod loop. See deploy().
    void applyWinnerOverrides(Profile& profile,
                              const QString& gameDir,
                              const Linker& linker,
                              DeployRecord& record,
                              ConflictIndex& conflicts,
                              QHash<QString, QString>& ciOwners);

    // Deploys one mod's files. Returns the number of files that FAILED to link.
    // record/conflicts are only updated for files that actually deployed.
    int deployMod(const QString& modId,
                  const QString& gameDir,
                  const Linker& linker,
                  DeployRecord& record,
                  ConflictIndex& conflicts,
                  QHash<QString, QString>& ciOwners);

    // Deploys the Overwrite folder last so it wins every conflict. Its contents
    // are Data-relative (relPath R -> gameDir/Data/R, recorded as "Data/R" owned by
    // overwriteOwnerId()). Returns the number of files that FAILED to link.
    int deployOverwrite(const QString& gameDir,
                        const QString& overwriteDir,
                        const Linker& linker,
                        DeployRecord& record,
                        ConflictIndex& conflicts,
                        QHash<QString, QString>& ciOwners);

    // Backup dir living inside the game dir; holds pre-existing (non-Solero)
    // originals that mods were deployed over, so undeploy can restore them.
    static QString backupDirName() { return ".solero-backup"; }
};

} // namespace solero
