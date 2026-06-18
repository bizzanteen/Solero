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
    // Returns true if the line is a recognized progress/phase line; false otherwise.
    static bool parseProgressLine(const QString& line, QString& op, double& pct);

    // Pure parser: scans the captured engine output for "Unable to download …"
    // failure lines and classifies each by its (Downloader+State|…) descriptor.
    // Unit-testable without a process.
    static QList<FailedArchive> parseFailedArchives(const QString& log);

    // True when the archive is a Creation Club file: the name, after stripping an
    // optional "Data_" prefix, starts with "cc" (case-insensitive).
    // e.g. "Data_ccbgssse037-curios.bsa" or "ccbgssse037-curios.esl".
    static bool isCreationClub(const QString& name);

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

    QProcess* m_proc = nullptr;
    QString m_log;  // full captured output of the current install, for failure parsing
};

} // namespace solero
