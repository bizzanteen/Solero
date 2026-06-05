#pragma once
#include "WabbajackModlist.h"
#include <QObject>
#include <QList>
#include <QString>

class QProcess;

namespace solero {

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

    // Parse a `[FILE_PROGRESS] <op>: <file> (<pct>%) ...` line.
    // Returns true and fills op/file/pct on match; false otherwise.
    static bool parseFileProgress(const QString& line, QString& op,
                                  QString& file, double& pct);

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

private:
    QProcess* m_proc = nullptr;
};

} // namespace solero
