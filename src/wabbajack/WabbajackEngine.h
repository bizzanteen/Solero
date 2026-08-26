#pragma once
#include "WabbajackModlist.h"
#include "DownloadEta.h"
#include <QElapsedTimer>
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

// What one progress line carries. `pct` is 0..100, or -1 for a line with no
// percentage. `remainingBytes` is what is still to transfer, or -1 when unknown.
// `phase` keeps the download rate and the install rate in separate estimators -
// they differ by orders of magnitude.
enum class ProgressPhase { Other, Downloading, Installing };

struct EngineProgress {
    QString op;
    double pct = -1;
    double remainingBytes = -1;
    ProgressPhase phase = ProgressPhase::Other;
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

    // Parse a jackify-engine progress line (\r-terminated):
    //   "Installing files 819/1497 (233.3MB/276.7MB) - ..."
    //   "Downloading Mod Archives (0/1) - 20.3MB/s - 0.1GB remaining"
    //   "=== <Phase> ===" banners
    static bool parseProgressLine(const QString& line, EngineProgress& out);

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

    // Remove the lowercase CC duplicates ensureLowercaseCCLinks() made, but only
    // where a differently-cased sibling shares the same inode - a real hard link,
    // so nothing is lost. A lowercase-only CC file with no sibling is always kept.
    // Returns the names removed.
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
    // GameFileSource asks for CC files by lowercase Bethesda id
    // ("ccbgssse037-curios.bsa") but Steam stores them mixed-case, so on a
    // case-sensitive fs the lookup misses and the install aborts. Hard-links
    // lowercase names into Data. Idempotent and never fatal.
    void ensureLowercaseCCLinks();

    // The install-time lowercase links break Mutagen tools afterwards - PGPatcher
    // sees two case-variants of the same master and fails - so drop them once the
    // engine has finished. Non-fatal.
    void removeRedundantLowercaseCCLinks();

    QProcess* m_proc = nullptr;
    QString m_log;  // full captured output of the current install, for failure parsing
    // Largest "<X> remaining" download figure seen this run, used as the effective
    // download total for a monotonic, size-based download percentage.
    double m_dlPeakRemaining = 0;
    // Time remaining, tracked per phase - a download runs at network speed and the
    // install at disk speed, so one estimator across both would be meaningless.
    QElapsedTimer m_runClock;
    EtaEstimator m_dlEta;
    EtaEstimator m_installEta;
};

} // namespace solero
