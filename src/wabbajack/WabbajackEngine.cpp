#include "WabbajackEngine.h"
#include "core/AppConfig.h"
#include "tools/ToolDownloader.h"

#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <memory>

namespace solero {

WabbajackEngine::WabbajackEngine(QObject* parent) : QObject(parent) {}

WabbajackEngine::~WabbajackEngine() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

QString WabbajackEngine::findEngine() {
    auto check = [](const QString& p) -> QString {
        if (p.isEmpty()) return QString();
        QFileInfo fi(p);
        return (fi.exists() && fi.isFile()) ? fi.absoluteFilePath() : QString();
    };

    // 1. Configured path.
    if (QString p = check(AppConfig::instance().jackifyEnginePath()); !p.isEmpty())
        return p;
    // 2. Environment override.
    if (QString p = check(qEnvironmentVariable("JACKIFY_ENGINE_PATH")); !p.isEmpty())
        return p;
    // 3. Conventional home install.
    if (QString p = check(QDir::homePath() + "/Jackify/jackify-engine/jackify-engine");
        !p.isEmpty())
        return p;
    // 4. AppImage bundle layout.
    if (const QString appdir = qEnvironmentVariable("APPDIR"); !appdir.isEmpty()) {
        if (QString p = check(appdir + "/opt/jackify/engine/jackify-engine"); !p.isEmpty())
            return p;
    }
    // 5. PATH.
    if (QString p = QStandardPaths::findExecutable("jackify-engine"); !p.isEmpty())
        return p;

    return QString();
}

QList<WabbajackModlist> WabbajackEngine::parseModlistsJson(const QByteArray& stdoutBytes,
                                                           QString* error) {
    QList<WabbajackModlist> out;
    int brace = stdoutBytes.indexOf('{');
    if (brace < 0) {
        if (error) *error = QStringLiteral("No JSON object found in engine output");
        return out;
    }
    QByteArray json = stdoutBytes.mid(brace);
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("JSON parse error: ") + pe.errorString();
        return out;
    }
    const QJsonArray arr = doc.object().value("modlists").toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        WabbajackModlist m;
        m.title       = o.value("title").toString();
        m.description = o.value("description").toString();
        m.author      = o.value("author").toString();
        m.machineUrl  = o.value("machineURL").toString();
        // The engine's `install -m` wants the namespaced "Author/ListName" form
        // (its "machineURL"), not the bare machineURL field in this JSON.
        m.namespacedName = o.value("namespacedName").toString();
        m.game        = o.value("game").toString();
        m.gameHuman   = o.value("gameHumanFriendly").toString();
        m.version     = o.value("version").toString();
        m.nsfw        = o.value("nsfw").toBool();
        m.official    = o.value("official").toBool();
        m.utility     = o.value("utilityList").toBool();

        const QJsonObject links = o.value("links").toObject();
        m.imageUrl   = links.value("image").toString();
        m.readmeUrl  = links.value("readme").toString();
        m.websiteUrl = links.value("websiteURL").toString();

        const QJsonObject sizes = o.value("sizes").toObject();
        m.downloadSizeStr = sizes.value("downloadSizeFormatted").toString();
        m.installSizeStr  = sizes.value("installSizeFormatted").toString();

        const QJsonArray tags = o.value("tags").toArray();
        for (const QJsonValue& t : tags) m.tags << t.toString();

        out << m;
    }
    return out;
}

bool WabbajackEngine::parseProgressLine(const QString& lineRaw, QString& op,
                                        double& pct) {
    const QString line = lineRaw.trimmed();
    if (line.isEmpty()) return false;

    // Phase banner: "=== Installing ===" - surface the phase as the op label, no pct.
    static const QRegularExpression phaseRe(
        QStringLiteral(R"(^===\s*(.+?)\s*===$)"));
    if (auto m = phaseRe.match(line); m.hasMatch()) {
        op  = m.captured(1).trimmed();
        pct = -1;
        return true;
    }

    // "Installing files 819/1497 (233.3MB/276.7MB) - ..." -> pct from n/total.
    static const QRegularExpression installRe(
        QStringLiteral(R"(^Installing files\s+(\d+)\s*/\s*(\d+))"));
    if (auto m = installRe.match(line); m.hasMatch()) {
        const double n = m.captured(1).toDouble();
        const double total = m.captured(2).toDouble();
        pct = (total > 0) ? (100.0 * n / total) : -1;
        op  = QStringLiteral("Installing files %1/%2")
                  .arg(m.captured(1), m.captured(2));
        return true;
    }

    // "Downloading Mod Archives (0/1) - 20.3MB/s - 0.1GB remaining" -> pct from n/total.
    static const QRegularExpression dlRe(
        QStringLiteral(R"(^Downloading\b.*?\((\d+)\s*/\s*(\d+)\))"));
    if (auto m = dlRe.match(line); m.hasMatch()) {
        const double n = m.captured(1).toDouble();
        const double total = m.captured(2).toDouble();
        pct = (total > 0) ? (100.0 * n / total) : -1;
        op  = QStringLiteral("Downloading archives %1/%2")
                  .arg(m.captured(1), m.captured(2));
        // Append "remaining" hint if present for a richer op label.
        static const QRegularExpression remRe(
            QStringLiteral(R"(-\s*([\d.]+\s*\wB remaining))"));
        if (auto r = remRe.match(line); r.hasMatch())
            op += QStringLiteral(" - %1").arg(r.captured(1).trimmed());
        return true;
    }

    return false;
}

static QProcessEnvironment engineEnv() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("DOTNET_SYSTEM_GLOBALIZATION_INVARIANT", "1");
    const QString key = ToolDownloader::nexusApiKey();
    if (!key.isEmpty()) env.insert("NEXUS_API_KEY", key);
    return env;
}

void WabbajackEngine::fetchModlists() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        emit failed(QStringLiteral("An engine process is already running"));
        return;
    }
    const QString exe = findEngine();
    if (exe.isEmpty()) {
        emit failed(QStringLiteral("jackify-engine not found"));
        return;
    }

    auto* proc = new QProcess(this);
    m_proc = proc;
    proc->setProgram(exe);
    proc->setArguments({"list-modlists", "-json", "-sort-by", "title"});
    proc->setWorkingDirectory(QFileInfo(exe).absolutePath());
    proc->setProcessEnvironment(engineEnv());

    connect(proc, &QProcess::finished, this,
            [this, proc](int exitCode, QProcess::ExitStatus status) {
        if (status == QProcess::CrashExit) {
            emit failed(QStringLiteral("Engine crashed while listing modlists"));
        } else if (exitCode != 0) {
            emit failed(QStringLiteral("Engine exited with code %1").arg(exitCode));
        } else {
            QString err;
            QList<WabbajackModlist> lists =
                parseModlistsJson(proc->readAllStandardOutput(), &err);
            if (!err.isEmpty()) emit failed(err);
            else emit modlistsReady(lists);
        }
        if (m_proc == proc) m_proc = nullptr;
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc](QProcess::ProcessError) {
        emit failed(QStringLiteral("Failed to start engine: ") + proc->errorString());
        if (m_proc == proc) m_proc = nullptr;
        proc->deleteLater();
    });

    proc->start();
}

void WabbajackEngine::install(const QString& machineUrlOrFile, bool isLocalFile,
                              const QString& installDir, const QString& downloadsDir) {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        emit failed(QStringLiteral("An engine process is already running"));
        return;
    }
    const QString exe = findEngine();
    if (exe.isEmpty()) {
        emit failed(QStringLiteral("jackify-engine not found"));
        return;
    }

    auto* proc = new QProcess(this);
    m_proc = proc;
    proc->setProgram(exe);
    QStringList args{"install"};
    args << (isLocalFile ? "-w" : "-m") << machineUrlOrFile
         << "-o" << installDir
         << "-d" << downloadsDir;
    proc->setArguments(args);
    proc->setWorkingDirectory(QFileInfo(exe).absolutePath());
    proc->setProcessEnvironment(engineEnv());
    proc->setProcessChannelMode(QProcess::MergedChannels);

    // The engine rewrites progress lines in place with bare '\r' (no '\n'), so
    // QProcess::canReadLine() won't split them. Read whatever bytes are available,
    // buffer any partial trailing fragment, and split on both '\r' and '\n'.
    auto buf = std::make_shared<QString>();
    auto drain = [this, proc, buf]() {
        proc->setReadChannel(QProcess::StandardOutput);
        *buf += QString::fromUtf8(proc->readAllStandardOutput());
        // Split into complete segments; keep the last (possibly partial) fragment.
        const auto segs = buf->split(QRegularExpression(QStringLiteral("[\\r\\n]")));
        for (int i = 0; i + 1 < segs.size(); ++i) {
            const QString line = segs.at(i).trimmed();
            if (line.isEmpty()) continue;
            emit logLine(line);
            QString op; double pct = -1;
            if (parseProgressLine(line, op, pct))
                emit progress(op, QString(), pct);
        }
        *buf = segs.isEmpty() ? QString() : segs.last();
    };
    connect(proc, &QProcess::readyReadStandardOutput, this, drain);

    connect(proc, &QProcess::finished, this,
            [this, proc, drain, buf](int exitCode, QProcess::ExitStatus status) {
        drain();
        // Flush any trailing fragment with no terminator.
        const QString tail = buf->trimmed();
        if (!tail.isEmpty()) {
            emit logLine(tail);
            QString op; double pct = -1;
            if (parseProgressLine(tail, op, pct))
                emit progress(op, QString(), pct);
        }
        bool ok = (status == QProcess::NormalExit && exitCode == 0);
        emit installFinished(ok, exitCode);
        if (m_proc == proc) m_proc = nullptr;
        proc->deleteLater();
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc](QProcess::ProcessError) {
        emit logLine(QStringLiteral("Failed to start engine: ") + proc->errorString());
        emit installFinished(false, -1);
        if (m_proc == proc) m_proc = nullptr;
        proc->deleteLater();
    });

    proc->start();
}

void WabbajackEngine::cancel() {
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(3000))
            m_proc->kill();
    }
}

} // namespace solero
