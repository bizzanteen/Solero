#pragma once
#include "core/Types.h"
#include "deploy/DeployRecord.h"
#include <QString>
#include <QDateTime>
namespace solero {
class ToolRunner {
public:
    struct Result { bool launched=false; QString error; QString output; };
    // gameDir/stagingDir used for output capture. Blocks until the tool exits.
    // When useGamescope is set and gamescopePath is non-empty, the assembled
    // launch command is wrapped in `gamescopePath <gamescopeArgs> -- …` so the
    // game runs inside a nested gamescope compositor (used by the onPlay path to
    // fix keyboard/mouse input grab under Wayland). Regular tool runs leave this
    // off and behave exactly as before.
    static Result run(const Executable& exe, const QString& gameDir, const QString& stagingRoot,
                      bool useGamescope = false, const QString& gamescopePath = {},
                      const QString& gamescopeArgs = {});

    // Wrap an inner argv (program first, then its args) in
    // `gamescopePath <gamescopeArgs> -- <innerArgv…>` when enabled and
    // gamescopePath is non-empty; otherwise returns innerArgv unchanged. Pure -
    // exposed for unit testing.
    static QStringList gamescopeWrappedArgv(bool enabled, const QString& gamescopePath,
                                            const QString& gamescopeArgs,
                                            const QStringList& innerArgv);

    // Move every file under captureBase whose mtime is >= runStart into destBase,
    // preserving its path relative to captureBase. Files owned by the deploy
    // record are skipped (their relPath is matched relative to gameDir, e.g.
    // "Data/SKSE/Plugins/foo.dll") so a deployed mod file isn't yanked into the
    // capture target just because its mtime was bumped. Returns the number of
    // files moved; appends a note to *warning if any move failed. Cross-fs safe
    // (rename -> copy+remove fallback). Exposed for unit testing.
    static int captureNewFiles(const QString& captureBase, const QString& destBase,
                               const QString& gameDir, const QDateTime& runStart,
                               const DeployRecord& record, QString* warning);
private:
    // Shell-style tokenizer that respects single and double quotes.
    static QStringList tokenizeArgs(const QString& s);
};
}
