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
    // Blocks until the tool exits. outputModFolder is the staging folder for
    // exe.outputModId, falling back to the id when empty. overwriteDir catches
    // writes when there's no output mod; empty means the legacy global one.
    static Result run(const Executable& exe, const QString& gameDir, const QString& stagingRoot,
                      const QString& outputModFolder = {}, const QString& overwriteDir = {});

    // Snapshot the absolute-path -> mtime(ms) of every file under captureBase. Taken
    // before launch so the post-run walk can distinguish a genuinely new/modified
    // file from a pre-existing unmanaged loose file that merely shares the launch
    // whole-second. Exposed for unit testing.
    static QHash<QString, qint64> snapshotMtimes(const QString& captureBase);

    // Move everything new or modified under captureBase into destBase, keeping
    // relative paths. New = absent from preSnapshot; modified = a strictly greater
    // mtime. Files the deploy record owns are skipped, so a deployed mod file isn't
    // yanked out just because its mtime moved. Returns the count moved.
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
