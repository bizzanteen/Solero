#pragma once
#include <QString>
#include <memory>

class QFile;

namespace solero {

class DeployRecord;

// Append-only companion to the deploy record. The record can't be rewritten
// mid-deploy (it would drop paths whose files aren't unlinked until the sweep at
// the end, orphaning them), so new links are appended here and folded back in by
// DeployRecord::loadFromFile. Discarded once the final record is written.
class DeployJournal {
public:
    DeployJournal() = default;
    ~DeployJournal();
    DeployJournal(const DeployJournal&) = delete;
    DeployJournal& operator=(const DeployJournal&) = delete;

    static QString pathFor(const QString& recordPath);

    // Truncates any journal from an earlier deploy; by now it has been folded
    // into the record we're diffing against. False if it couldn't be opened.
    bool begin(const QString& recordPath);

    void append(const QString& relPath, const QString& modId, qint64 size, qint64 mtimeMs);

    void flush();

    // Only safe once the record on disk covers everything the journal did.
    void discard();

    // Skips unparsable lines - an interrupted journal's last line may be torn.
    static void mergeInto(DeployRecord& record, const QString& recordPath);

private:
    std::unique_ptr<QFile> m_file;
    QString m_path;
};

} // namespace solero
