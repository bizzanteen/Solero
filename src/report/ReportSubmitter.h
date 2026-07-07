#pragma once
// Solero bug/crash report submitter. Builds a JSON payload from the user's answers
// + redacted log tail + system info, and POSTs it to a serverless relay (Cloudflare
// Worker) that holds the GitHub token and creates the issue. The client never sees
// the token, so users need no GitHub account.
//
// buildPayload()/gatherLogTail()/gatherSystemInfo() are pure and network-free so the
// payload assembly can be unit-tested without a live relay; only submit() touches the
// network.
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace solero {

// The relay endpoint reports are POSTed to (a Cloudflare Worker that holds the GitHub
// token and files the issue). Override at runtime with the SOLERO_REPORT_RELAY env var.
inline constexpr const char* kReportRelayUrl =
    "https://solero-report-relay.solero.workers.dev/report";

// Target GitHub repo, used for the browser-fallback URL (github.com/<owner>/<repo>/
// issues/new). The relay holds the authoritative repo for authenticated submits.
inline constexpr const char* kReportRepo = "bizzanteen/Solero";

// Shared token sent as the X-Solero-Report header; the Worker checks it as a light
// anti-abuse gate. Kept in step with the Worker's SHARED_TOKEN secret.
inline constexpr const char* kReportSharedToken = "8dfdabefb9f88efc4f8305ed4111736b22c3fe9a23db519f";

class ReportSubmitter : public QObject {
    Q_OBJECT
public:
    enum class Kind { Issue, Crash };

    // System context attached to every report.
    struct SystemInfo {
        QString appVersion;
        QString os;
        QString qt;
        QString deployMode;
        int     modCount = -1; // -1 = unknown / no active profile
    };

    explicit ReportSubmitter(QObject* parent = nullptr);

    // Assemble the wire payload: { kind, title, sections{version,os,qt,deployMode,
    // modCount}, body, log }. `fields` are the user's answers (summary / whatHappened
    // / expected / steps, or the crash "whatDoing"); `logTail` is already redacted.
    // Pure + deterministic given its inputs - unit-tested, no network.
    static QJsonObject buildPayload(Kind kind,
                                    const QMap<QString, QString>& fields,
                                    const QString& logTail,
                                    const SystemInfo& info);

    // Gather system info from Qt + AppConfig. deployMode/modCount come from the active
    // profile (passed in by the caller that has it); pass modCount<0 when unknown.
    static SystemInfo gatherSystemInfo(int modCount = -1);

    // Read the last ~15 KB of logFilePath() and redact it. `startOffset` > 0 (a crash
    // marker's recorded offset) trims everything before the crash section.
    static QString gatherLogTail(qint64 startOffset = 0);

    // The relay URL actually in effect: SOLERO_REPORT_RELAY env override, else the
    // kReportRelayUrl constant.
    static QString relayUrl();
    // True when the effective relay URL is non-empty (reporting can post).
    static bool relayConfigured();

    // Browser fallback: a prefilled github.com/<repo>/issues/new URL from the payload.
    static QString prefilledIssueUrl(const QJsonObject& payload);

    // Async HTTPS post of `payload` to relayUrl(). Emits succeeded(issueUrl) on a 2xx
    // with a JSON { issueUrl }, else failed(error). Guards nothing itself - the dialog
    // disables Send while in flight.
    void submit(const QJsonObject& payload);

signals:
    void succeeded(const QString& issueUrl);
    void failed(const QString& error);

private:
    QNetworkAccessManager* m_net = nullptr;
};

} // namespace solero
