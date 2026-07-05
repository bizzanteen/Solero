#include "report/ReportSubmitter.h"

#include "core/AppConfig.h"
#include "core/Log.h"
#include "deploy/DeployMode.h"
#include "report/Redactor.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrl>
#include <QUrlQuery>

namespace solero {

namespace {
QString fieldOr(const QMap<QString, QString>& f, const QString& key, const QString& fallback) {
    const QString v = f.value(key).trimmed();
    return v.isEmpty() ? fallback : v;
}
} // namespace

ReportSubmitter::ReportSubmitter(QObject* parent) : QObject(parent) {}

QJsonObject ReportSubmitter::buildPayload(Kind kind,
                                          const QMap<QString, QString>& fields,
                                          const QString& logTail,
                                          const SystemInfo& info) {
    const bool crash = (kind == Kind::Crash);

    // Title
    QString summary = fields.value("summary").trimmed();
    QString title;
    if (crash) {
        if (summary.isEmpty()) {
            // First non-empty line of the log tail (usually the "=== CRASH: … ===" marker).
            const auto lines = logTail.split(QChar('\n'));
            for (const QString& ln : lines) {
                const QString t = ln.trimmed();
                if (!t.isEmpty()) { summary = t; break; }
            }
        }
        if (summary.isEmpty()) summary = QStringLiteral("Solero crashed");
        title = QStringLiteral("[crash] ") + summary;
    } else {
        if (summary.isEmpty()) summary = QStringLiteral("(no summary)");
        title = QStringLiteral("[bug] ") + summary;
    }

    // Body (markdown of the user's answers)
    QString body;
    if (crash) {
        body += QStringLiteral("**What I was doing when it crashed**\n\n");
        body += fieldOr(fields, "whatDoing", QStringLiteral("(not provided)"));
    } else {
        body += QStringLiteral("**What happened**\n\n");
        body += fieldOr(fields, "whatHappened", QStringLiteral("(not provided)"));
        body += QStringLiteral("\n\n**Expected**\n\n");
        body += fieldOr(fields, "expected", QStringLiteral("(not provided)"));
        const QString steps = fields.value("steps").trimmed();
        if (!steps.isEmpty())
            body += QStringLiteral("\n\n**Steps to reproduce**\n\n") + steps;
    }

    QJsonObject sections{
        {QStringLiteral("version"),    info.appVersion},
        {QStringLiteral("os"),         info.os},
        {QStringLiteral("qt"),         info.qt},
        {QStringLiteral("deployMode"), info.deployMode},
        {QStringLiteral("modCount"),   info.modCount},
    };

    return QJsonObject{
        {QStringLiteral("kind"),     crash ? QStringLiteral("crash") : QStringLiteral("bug")},
        {QStringLiteral("title"),    title},
        {QStringLiteral("sections"), sections},
        {QStringLiteral("body"),     body.trimmed()},
        {QStringLiteral("log"),      logTail},
    };
}

ReportSubmitter::SystemInfo ReportSubmitter::gatherSystemInfo(int modCount) {
    SystemInfo si;
    si.appVersion = QCoreApplication::applicationVersion();
    si.os         = QSysInfo::prettyProductName();
    si.qt         = QString::fromLatin1(qVersion());
    switch (AppConfig::instance().deployMode()) {
        case DeployMode::SymLink:  si.deployMode = QStringLiteral("symlink");  break;
        case DeployMode::Copy:     si.deployMode = QStringLiteral("copy");     break;
        case DeployMode::HardLink: si.deployMode = QStringLiteral("hardlink"); break;
    }
    si.modCount = modCount;
    return si;
}

QString ReportSubmitter::gatherLogTail(qint64 startOffset) {
    QFile f(logFilePath());
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const qint64 size = f.size();
    constexpr qint64 kTail = 15 * 1024; // normal: last ~15 KB
    constexpr qint64 kCap  = 64 * 1024; // hard cap when honouring a crash offset

    qint64 from;
    if (startOffset > 0 && startOffset < size) {
        from = startOffset;
        if (size - from > kCap) from = size - kCap; // guard against a huge span
    } else {
        from = size > kTail ? size - kTail : 0;
    }
    if (!f.seek(from)) return QString();
    const QByteArray data = f.readAll();
    return redact(QString::fromUtf8(data));
}

QString ReportSubmitter::relayUrl() {
    const QString env = qEnvironmentVariable("SOLERO_REPORT_RELAY");
    if (!env.isEmpty()) return env;
    return QString::fromLatin1(kReportRelayUrl);
}

bool ReportSubmitter::relayConfigured() {
    return relayUrl() != QString::fromLatin1(kReportRelayUrl);
}

QString ReportSubmitter::prefilledIssueUrl(const QJsonObject& payload) {
    const QString title  = payload.value(QStringLiteral("title")).toString();
    const QJsonObject s  = payload.value(QStringLiteral("sections")).toObject();

    QString body;
    body += QStringLiteral("| field | value |\n|---|---|\n");
    body += QStringLiteral("| version | %1 |\n").arg(s.value("version").toString());
    body += QStringLiteral("| os | %1 |\n").arg(s.value("os").toString());
    body += QStringLiteral("| qt | %1 |\n").arg(s.value("qt").toString());
    body += QStringLiteral("| deploy mode | %1 |\n").arg(s.value("deployMode").toString());
    body += QStringLiteral("| mods | %1 |\n\n").arg(s.value("modCount").toInt());
    body += payload.value(QStringLiteral("body")).toString();
    body += QStringLiteral("\n\n_(Log not attached via the browser fallback - paste the "
                           "relevant lines from ~/.local/share/solero/logs/solero.log if you can.)_");

    QUrl url(QStringLiteral("https://github.com/") + QString::fromLatin1(kReportRepo) +
             QStringLiteral("/issues/new"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"), title);
    q.addQueryItem(QStringLiteral("body"),  body);
    url.setQuery(q);
    return url.toString(QUrl::FullyEncoded);
}

void ReportSubmitter::submit(const QJsonObject& payload) {
    if (!m_net) m_net = new QNetworkAccessManager(this);

    QNetworkRequest req{QUrl(relayUrl())};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("X-Solero-Report", QByteArray(kReportSharedToken));

    // NOTE: never log the payload - it carries the (redacted) log and user text; the
    // shared token above must likewise never be echoed.
    qCInfo(lcApp) << "submitting report to relay";

    QNetworkReply* reply = m_net->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray raw = reply->readAll();
        const QJsonObject obj = QJsonDocument::fromJson(raw).object();

        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString msg = obj.contains("error") ? obj.value("error").toString()
                                                : reply->errorString();
            if (status) msg = QStringLiteral("HTTP %1 - %2").arg(status).arg(msg);
            qCWarning(lcApp) << "report submit failed:" << msg;
            emit failed(msg);
            return;
        }
        const QString issueUrl = obj.value(QStringLiteral("issueUrl")).toString();
        if (issueUrl.isEmpty()) {
            emit failed(QStringLiteral("The relay accepted the report but returned no issue URL."));
            return;
        }
        qCInfo(lcApp) << "report submitted; issue created";
        emit succeeded(issueUrl);
    });
}

} // namespace solero
