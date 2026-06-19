#pragma once
#include "WabbajackModlist.h"
#include <QObject>
#include <QList>
#include <QString>

class QProcess;

namespace solero {

// Classification of a download source that the engine failed to fetch from.
enum class FailedSource { GameFileSource, Nexus, Mega, WabbajackCDN, Http, Other };

// One archive the engine was unable to download, parsed from the failure log.
// `game`/`version`/`path` are populated for GameFileSource (Creation-Kit) rows;
// `url` is populated for the network sources (Mega/WabbajackCDN/Http) when present.
struct FailedArchive {
    QString name;
    FailedSource source = FailedSource::Other;
    QString game;
    QString version;
    QString path;
    QString url;
    // File is present on disk but its hash doesn't match (wrong version, e.g. a
    // Steam-shipped Creation Club file vs the required in-game Creations build),
    // as opposed to the file being missing entirely. Set from the VFS-priming line.
    bool wrongHash = false;
};

// Wraps the jackify-engine CLI (a Linux-native .NET fork of wabbajack-cli)
// as a subprocess: lists the gallery and drives installs. Async via QProcess.
class WabbajackEngine : public QObject {
    Q_OBJECT
public:
    explicit WabbajackEngine(QObject* parent = nullptr);
    ~WabbajackEngine() override;

    // Discovery order; returns first existing executable, or "" if none found.
    static QString findEngine();
    static bool available() { return !findEngine().isEmpty(); }

    // Pure parser: skips leading log lines, parses the first JSON object and
    // its `modlists[]` array into WabbajackModlist. Unit-testable w/o a process.
    static QList<WabbajackModlist> parseModlistsJson(const QByteArray& stdoutBytes,
                                                     QString* error = nullptr);

    // Parse a real jackify-engine progress line. The engine emits (\r-terminated):
    //   "Installing files 819/1497 (233.3MB/276.7MB) - ..."
    //   "Downloading Mod Archives (0/1) - 20.3MB/s - 0.1GB remaining"
    //   "=== <Phase> ===" banners
    // On match, fills `op` with a human label and `pct` with a 0..100 percentage
    // (pct < 0 means "no percentage for this line, but the op label is valid").
    // `remainingBytes` is set to the parsed "<X> remaining" figure for download
    // lines (so the caller can compute a size-based pct from the run's peak), and
    // -1 for every other line.
    // Returns true if the line is a recognized progress/phase line; false otherwise.
    static bool parseProgressLine(const QString& line, QString& op, double& pct,
                                  double& remainingBytes);

    // Parse a human size token ("233.3MB", "0.1 GB", "1024KB", case-insensitive,
    // optional whitespace) into bytes (1024-based units). Returns -1 if unparseable.
    static double parseSizeToBytes(const QString& token);

    // Pure parser: scans the captured engine output for "Unable to download …"
    // failure lines and classifies each by its (Downloader+State|…) descriptor.
    // Unit-testable without a process.
    static QList<FailedArchive> parseFailedArchives(const QString& log);

    // True when the archive is a Creation Club file: the name, after stripping an
    // optional "Data_" prefix, starts with "cc" (case-insensitive).
    // e.g. "Data_ccbgssse037-curios.bsa" or "ccbgssse037-curios.esl".
    static bool isCreationClub(const QString& name);

    // Remove the redundant all-lowercase Creation Club hard-link duplicates created
    // by ensureLowercaseCCLinks() from `dataDir`, but only when it is provably safe:
    // a differently-cased sibling exists in the same dir and both paths share the
    // same inode/device (a true hard-link duplicate, so no data is ever lost). A
    // lowercase-only CC file with no proper-case sibling (e.g. Rare Curios'
    // "ccbgssse037-curios.esl") is always KEPT. Static + dir-parameterized for
    // testability; the instance method below resolves the game Data dir and calls it.
    // Returns the names of the lowercase files actually removed.
    static QStringList removeRedundantLowercaseCCLinks(const QString& dataDir);

    // Async: run `list-modlists -json -sort-by title`, emit modlistsReady/failed.
    void fetchModlists();

    // Async: run install. isLocalFile selects `-w <file>` vs `-m <machineUrl>`.
    void install(const QString& machineUrlOrFile, bool isLocalFile,
                 const QString& installDir, const QString& downloadsDir);

    // Terminate/kill a running install (or fetch) process if any.
    void cancel();

signals:
    void modlistsReady(const QList<WabbajackModlist>& modlists);
    void failed(const QString& error);
    void logLine(const QString& line);
    void progress(const QString& op, const QString& file, double pct);
    void installFinished(bool ok, int exitCode);
    // Emitted (in addition to installFinished(false,…)) when an install fails,
    // carrying the archives parsed out of the captured log so the UI can show a
    // classified, actionable report. `failed` may be empty for unknown failures.
    void installFailed(int exitCode, const QList<FailedArchive>& failed);

private:
    // Pre-install fix for case-sensitive filesystems (btrfs/ext4): the engine's
    // GameFileSource downloader requests Skyrim Creation Club files by their
    // lowercase Bethesda IDs (e.g. "ccbgssse037-curios.bsa"), but Steam stores
    // them mixed-case on disk ("ccBGSSSE037-Curios.bsa"). On Linux the lowercase
    // lookup misses and the install aborts. This creates lowercase-named hard
    // links in the Skyrim Data dir so the lowercase names resolve. Idempotent and
    // non-fatal: never aborts the install.
    void ensureLowercaseCCLinks();

    // Post-install counterpart to ensureLowercaseCCLinks(): once jackify-engine has
    // finished, the install-time lowercase CC hard links are no longer needed and
    // actively break Mutagen-based tools (PGPatcher sees two case-variants of the
    // same master and fails). Resolves the Skyrim Data dir and delegates to the
    // static helper above. Non-fatal.
    void removeRedundantLowercaseCCLinks();

    QProcess* m_proc = nullptr;
    QString m_log;  // full captured output of the current install, for failure parsing
    // Largest "<X> remaining" download figure seen this run, used as the effective
    // download total for a monotonic, size-based download percentage.
    double m_dlPeakRemaining = 0;
};

} // namespace solero
