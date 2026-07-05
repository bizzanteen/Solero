#include "core/Log.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QString>
#include <QtGlobal>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#if defined(__GLIBC__)
#  include <execinfo.h> // backtrace(), backtrace_symbols_fd()
#  include <fcntl.h>    // open(), O_* flags - for the async-signal-safe crash marker
#  include <unistd.h>   // write(), lseek()
#  define SOLERO_HAVE_BACKTRACE 1
#endif

// Every subsystem category defaults to QtInfoMsg: qCInfo/qCWarning/qCCritical are ON,
// qCDebug is off (compiled in, runtime-suppressed -> zero cost). Keep per-item / hot-loop
// tracing behind qCDebug so it never runs unless explicitly enabled.
Q_LOGGING_CATEGORY(lcApp,      "solero.app",      QtInfoMsg)
Q_LOGGING_CATEGORY(lcDeploy,   "solero.deploy",   QtInfoMsg)
Q_LOGGING_CATEGORY(lcLoot,     "solero.loot",     QtInfoMsg)
Q_LOGGING_CATEGORY(lcInstall,  "solero.install",  QtInfoMsg)
Q_LOGGING_CATEGORY(lcFomod,    "solero.fomod",    QtInfoMsg)
Q_LOGGING_CATEGORY(lcNexus,    "solero.nexus",    QtInfoMsg)
Q_LOGGING_CATEGORY(lcDownload, "solero.download", QtInfoMsg)
Q_LOGGING_CATEGORY(lcProfile,  "solero.profile",  QtInfoMsg)
Q_LOGGING_CATEGORY(lcTools,    "solero.tools",    QtInfoMsg)
Q_LOGGING_CATEGORY(lcShader,   "solero.shader",   QtInfoMsg)

namespace {

QMutex g_mutex;          // serialises writes; the sink runs off the GUI thread (deploy/download)
QFile* g_logFile = nullptr;
int    g_logFd = -1;     // raw fd of g_logFile, for the async-signal crash handler
QtMessageHandler g_prevHandler = nullptr;
QString g_logPath;
bool g_installed = false;

// Crash-marker state. g_markerPath is a plain C buffer (filled at install time) so the
// async-signal crash handler can open() it without touching QString/malloc.
char   g_markerPath[4096] = {0};
bool   g_lastRunCrashed = false; // set at startup from a marker left by a prior crash
qint64 g_crashLogOffset = 0;     // byte offset into solero.log where that crash begins

// Kept in step with AppConfig::dataRoot() (~/.local/share/solero) but computed
// directly so this foundational TU has no dependency beyond QtCore.
QString logDir()  { return QDir::homePath() + "/.local/share/solero/logs"; }
QString logPath() { return logDir() + "/solero.log"; }
QString markerPath() { return logDir() + "/last-run-crashed"; }

// Read (and consume) a crash marker left by a previous run. Runs once at startup -
// ordinary QFile is fine here (not inside a signal handler). Sets the one-shot
// lastRunCrashed()/crashLogOffset() state, then deletes the marker.
void readCrashMarker() {
    QFile f(markerPath());
    if (!f.exists()) return;
    g_lastRunCrashed = true;
    if (f.open(QIODevice::ReadOnly)) {
        bool ok = false;
        const qint64 v = QString::fromLatin1(f.readAll().trimmed()).toLongLong(&ok);
        if (ok) g_crashLogOffset = v;
        f.close();
    }
    f.remove(); // one-shot: never re-prompt for the same crash
}

// Keep at most a few generations. Called once at startup, before the file is opened,
// so this is a plain rename chain (no per-write stat cost).
void rotateIfLarge() {
    const QString base = logPath();
    QFileInfo fi(base);
    if (!fi.exists() || fi.size() < 5 * 1024 * 1024) return; // ~5 MB
    QFile::remove(base + ".3");
    QFile::rename(base + ".2", base + ".3");
    QFile::rename(base + ".1", base + ".2");
    QFile::rename(base, base + ".1");
}

void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const QString line = qFormatLogMessage(type, ctx, msg) + QChar('\n');
    const QByteArray utf8 = line.toUtf8();
    {
        QMutexLocker lock(&g_mutex);
        if (g_logFile && g_logFile->isOpen()) {
            g_logFile->write(utf8);
            g_logFile->flush();
        }
    }
    // Always echo to stderr too (harmless when no terminal; useful when run from one).
    std::fwrite(utf8.constData(), 1, size_t(utf8.size()), stderr);
    std::fflush(stderr);
    if (type == QtFatalMsg) std::abort();
}

#if defined(SOLERO_HAVE_BACKTRACE)
// Async-signal-safe: write a marker file recording where in solero.log this crash's
// section begins, so the NEXT run can detect the crash and attach just that tail. Uses
// only open()/write()/close() and a hand-rolled integer-to-decimal (no malloc, no
// snprintf, no QString).
void writeCrashMarker(long long off) {
    if (g_markerPath[0] == '\0') return;
    const int fd = ::open(g_markerPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    char buf[32];
    int i = int(sizeof(buf));
    if (off <= 0) {
        buf[--i] = '0';
    } else {
        while (off > 0 && i > 0) { buf[--i] = char('0' + (off % 10)); off /= 10; }
    }
    (void)!::write(fd, buf + i, size_t(int(sizeof(buf)) - i));
    ::close(fd);
}

// Async-signal-safe as far as practical: only write()/backtrace_symbols_fd to the
// already-open log fd, then restore the default handler and re-raise so the process
// still dies with the right signal (and any core dump). No malloc, no mutex.
void crashHandler(int sig) {
    const char* name = "signal";
    switch (sig) {
        case SIGSEGV: name = "\n=== CRASH: SIGSEGV (segfault) ===\n"; break;
        case SIGABRT: name = "\n=== CRASH: SIGABRT (abort) ===\n";    break;
        case SIGBUS:  name = "\n=== CRASH: SIGBUS ===\n";             break;
        case SIGFPE:  name = "\n=== CRASH: SIGFPE ===\n";             break;
        default:      name = "\n=== CRASH: signal ===\n";            break;
    }
    if (g_logFd >= 0) {
        // Record the crash-section start (current append position) before writing it.
        writeCrashMarker(static_cast<long long>(::lseek(g_logFd, 0, SEEK_END)));
        (void)!::write(g_logFd, name, std::strlen(name));
        void* frames[64];
        int n = backtrace(frames, 64);
        backtrace_symbols_fd(frames, n, g_logFd);
    }
    (void)!::write(STDERR_FILENO, name, std::strlen(name));
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void terminateHandler() {
    static const char* m = "\n=== CRASH: std::terminate (unhandled exception) ===\n";
    if (g_logFd >= 0) {
        writeCrashMarker(static_cast<long long>(::lseek(g_logFd, 0, SEEK_END)));
        (void)!::write(g_logFd, m, std::strlen(m));
        void* frames[64];
        int n = backtrace(frames, 64);
        backtrace_symbols_fd(frames, n, g_logFd);
    }
    std::abort();
}
#endif

} // namespace

namespace solero {

void installLogging() {
    if (g_installed) return;
    g_installed = true;

    QDir().mkpath(logDir());

    // Detect a crash from the previous run before any rotation, and stash the marker
    // path for the async-signal handler. If the last run crashed, skip rotation so the
    // crashed log stays intact for the report dialog to read.
    {
        const QByteArray mp = markerPath().toLocal8Bit();
        std::strncpy(g_markerPath, mp.constData(), sizeof(g_markerPath) - 1);
    }
    readCrashMarker();
    if (!g_lastRunCrashed) rotateIfLarge();

    g_logPath = logPath();
    g_logFile = new QFile(g_logPath);
    if (g_logFile->open(QIODevice::Append | QIODevice::Text)) {
        g_logFd = g_logFile->handle();
    } else {
        // Couldn't open the file - fall back to stderr-only logging.
        delete g_logFile;
        g_logFile = nullptr;
        g_logFd = -1;
    }

    // Timestamp + category + level + message. %{category} is "default" for plain
    // qDebug()/qWarning() (uncategorised) and "solero.<x>" for our categories.
    qSetMessagePattern(
        "%{time yyyy-MM-dd HH:mm:ss.zzz} [%{category}] "
        "%{if-debug}DEBUG%{endif}%{if-info}INFO%{endif}"
        "%{if-warning}WARN%{endif}%{if-critical}CRIT%{endif}%{if-fatal}FATAL%{endif} "
        "%{message}");

    g_prevHandler = qInstallMessageHandler(messageHandler);

#if defined(SOLERO_HAVE_BACKTRACE)
    if (g_logFd >= 0) {
        std::signal(SIGSEGV, crashHandler);
        std::signal(SIGABRT, crashHandler);
        std::signal(SIGBUS,  crashHandler);
        std::signal(SIGFPE,  crashHandler);
        std::set_terminate(terminateHandler);
    }
#endif
}

void setVerboseLogging(bool on) {
    // Flip every solero.* debug category. Leave the framework's own categories alone.
    QLoggingCategory::setFilterRules(on ? QStringLiteral("solero.*.debug=true")
                                        : QStringLiteral("solero.*.debug=false"));
}

QString logFilePath() { return g_logPath; }

bool lastRunCrashed() { return g_lastRunCrashed; }

qint64 crashLogOffset() { return g_crashLogOffset; }

} // namespace solero
