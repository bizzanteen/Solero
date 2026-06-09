#pragma once
#include "core/Types.h"
#include "deploy/DeployRecord.h"
#include <QString>
#include <QDateTime>
#include <QHash>
namespace solero {
class ToolRunner {
public:
    struct Result { bool launched=false; QString error; QString output; };
    // gameDir/stagingDir used for output capture. Blocks until the tool exits.
    // outputModFolder is the on-disk staging folder name for exe.outputModId
    // (resolve via Profile::stagingFolderFor); when empty it falls back to the
    // id so callers that haven't migrated still work.
    // overwriteDir is the capture sink for files written when there's no output mod
    // (per-profile; see AppConfig::overwriteDir). Empty -> legacy global overwrite.
    static Result run(const Executable& exe, const QString& gameDir, const QString& stagingRoot,
                      const QString& outputModFolder = {}, const QString& overwriteDir = {});

    // Snapshot the absolute-path -> mtime(ms) of every file under captureBase. Taken
    // before launch so the post-run walk can distinguish a genuinely new/modified
    // file from a pre-existing unmanaged loose file that merely shares the launch
    // whole-second. Exposed for unit testing.
    static QHash<QString, qint64> snapshotMtimes(const QString& captureBase);

    // Move every file under captureBase that is genuinely new or MODIFIED into
    // destBase, preserving its path relative to captureBase. A file qualifies when
    // it is absent from `preSnapshot` (new) OR its mtime is strictly greater than
    // its snapshot value (modified). The whole-second-floored runStart still widens
    // the window for genuinely new files. Files owned by the deploy record are
    // skipped (relPath matched relative to gameDir, e.g. "Data/SKSE/Plugins/foo.dll")
    // so a deployed mod file isn't yanked into the capture target just because its
    // mtime was bumped. Returns the number of files moved; appends a note to
    // *warning if any move failed. Cross-fs safe (rename -> copy+remove fallback).
    static int captureNewFiles(const QString& captureBase, const QString& destBase,
                               const QString& gameDir, const QDateTime& runStart,
                               const DeployRecord& record,
                               const QHash<QString, qint64>& preSnapshot,
                               QString* warning);
private:
    // Shell-style tokenizer that respects single and double quotes.
    static QStringList tokenizeArgs(const QString& s);
};
}
