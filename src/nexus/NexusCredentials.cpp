#include "NexusCredentials.h"
#include "core/Log.h"
#include <QProcess>
#include <QStandardPaths>

namespace solero {
namespace {

// Attribute pair every Solero entry carries, so a search can find ours and only ours.
const char* kService = "solero-nexus";

QString toolPath() {
    return QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
}

// Run secret-tool, optionally feeding `stdin`. Returns false on a non-zero exit.
bool runTool(const QStringList& args, QString* out = nullptr,
             const QString& stdinData = QString()) {
    const QString exe = toolPath();
    if (exe.isEmpty()) return false;
    QProcess p;
    p.start(exe, args);
    if (!p.waitForStarted(5000)) return false;
    if (!stdinData.isEmpty()) {
        p.write(stdinData.toUtf8());
        p.closeWriteChannel();
    } else {
        p.closeWriteChannel();
    }
    if (!p.waitForFinished(15000)) { p.kill(); return false; }
    if (out) *out = QString::fromUtf8(p.readAllStandardOutput());
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

} // namespace

QString NexusCredentials::parseLoginAttribute(const QString& searchOutput) {
    for (const QString& line : searchOutput.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (!t.startsWith(QLatin1String("attribute.login"))) continue;
        const int eq = t.indexOf(QLatin1Char('='));
        if (eq < 0) continue;
        return t.mid(eq + 1).trimmed();
    }
    return {};
}

bool NexusCredentials::keyringAvailable() {
    if (toolPath().isEmpty()) return false;
    // A search that matches nothing still exits 0 when the service answers, and
    // fails when there is no Secret Service on the bus.
    return runTool({QStringLiteral("search"), QStringLiteral("service"),
                    QStringLiteral("solero-nexus-probe")});
}

QString NexusCredentials::savedLogin() {
    QString out;
    if (!runTool({QStringLiteral("search"), QStringLiteral("service"),
                  QLatin1String(kService)}, &out))
        return {};
    return parseLoginAttribute(out);
}

QString NexusCredentials::password(const QString& login) {
    if (login.isEmpty()) return {};
    QString out;
    if (!runTool({QStringLiteral("lookup"), QStringLiteral("service"),
                  QLatin1String(kService), QStringLiteral("login"), login}, &out))
        return {};
    // lookup writes the secret raw; only a trailing newline is ours to drop.
    if (out.endsWith(QLatin1Char('\n'))) out.chop(1);
    return out;
}

bool NexusCredentials::save(const QString& login, const QString& password) {
    if (login.isEmpty() || password.isEmpty()) return false;
    const bool ok = runTool({QStringLiteral("store"), QStringLiteral("--label"),
                             QStringLiteral("Solero - Nexus Mods"),
                             QStringLiteral("service"), QLatin1String(kService),
                             QStringLiteral("login"), login},
                            nullptr, password + QLatin1Char('\n'));
    qCInfo(lcNexus) << "nexus login" << (ok ? "saved to keyring" : "could not be saved");
    return ok;
}

bool NexusCredentials::forget(const QString& login) {
    if (login.isEmpty()) return false;
    return runTool({QStringLiteral("clear"), QStringLiteral("service"),
                    QLatin1String(kService), QStringLiteral("login"), login});
}

} // namespace solero
