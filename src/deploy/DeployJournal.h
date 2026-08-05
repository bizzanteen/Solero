#pragma once
#include <QString>
#include <memory>

class QFile;

namespace solero {

class DeployRecord;

// Append-only companion to the deploy record.
//
// A deploy must never shrink the on-disk record ahead of the filesystem. A record
// that has forgotten a file which is still in the game dir cannot be acted on by
// any later deploy or undeploy - both work from the record - so that file is
// orphaned permanently. That rules out rewriting the record part-way through a
// deploy, because the new record legitimately omits the files a disabled mod left
// behind: those aren't unlinked until the stale sweep at the very end.
//
// So the record file is left untouched for the whole deploy (it still describes
// every file physically present from the previous one) and each newly linked path
// is appended here instead. DeployRecord::loadFromFile() folds the journal back
// in, so a deploy interrupted at any point still reads back as a superset of what
// is on disk. The journal is discarded once the final record has been written.
class DeployJournal {
public:
    DeployJournal() = default;
    ~DeployJournal();
    DeployJournal(const DeployJournal&) = delete;
    DeployJournal& operator=(const DeployJournal&) = delete;

    static QString pathFor(const QString& recordPath);

    // Start (or restart) journalling next to the given record. Truncates a journal
    // left by an earlier interrupted deploy - by this point it has already been
    // folded into the record that deploy is diffing against. Returns false if the
    // journal could not be opened, in which case append/flush do nothing.
    bool begin(const QString& recordPath);

    void append(const QString& relPath, const QString& modId, qint64 size, qint64 mtimeMs);

    // Push buffered entries to the OS. Call wherever the record used to be flushed.
    void flush();

    // Close and delete the journal. Only correct once the record on disk covers
    // everything the journal did.
    void discard();

    // Apply a journal sitting next to recordPath, if any, on top of record.
    // Unparsable lines are skipped: the last line of an interrupted journal may be
    // half-written.
    static void mergeInto(DeployRecord& record, const QString& recordPath);

private:
    std::unique_ptr<QFile> m_file;
    QString m_path;
};

} // namespace solero
