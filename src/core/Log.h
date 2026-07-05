#pragma once
// Solero logging - per-subsystem QLoggingCategory + a file sink. Categories default to
// QtInfoMsg, so qCDebug is compiled in but suppressed at runtime (near-free); put
// per-item / hot-path tracing behind qCDebug. Raise a category with QT_LOGGING_RULES
// (e.g. "solero.deploy.debug=true") or solero::setVerboseLogging(true).
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcDeploy)
Q_DECLARE_LOGGING_CATEGORY(lcLoot)
Q_DECLARE_LOGGING_CATEGORY(lcInstall)
Q_DECLARE_LOGGING_CATEGORY(lcFomod)
Q_DECLARE_LOGGING_CATEGORY(lcNexus)
Q_DECLARE_LOGGING_CATEGORY(lcDownload)
Q_DECLARE_LOGGING_CATEGORY(lcProfile)
Q_DECLARE_LOGGING_CATEGORY(lcTools)
Q_DECLARE_LOGGING_CATEGORY(lcShader)

namespace solero {

// Install the file sink + message pattern + crash handler. Call once, as early as
// possible in main() (before constructing the application, so early messages are
// caught). Idempotent - a second call is a no-op. Safe to call before QApplication
// exists (the log path derives from the home dir, not QStandardPaths).
void installLogging();

// Flip every solero.* debug category on/off at runtime (Settings "verbose logging").
void setVerboseLogging(bool on);

// Absolute path of the active log file (…/.local/share/solero/logs/solero.log).
QString logFilePath();

// True if the previous run ended in a crash the handler caught (SIGSEGV/ABRT/BUS/FPE
// or std::terminate). Determined once, at installLogging(), by the presence of a
// marker file the crash handler writes before re-raising; the marker is deleted on
// read, so this is one-shot per crashed run (no false positives on SIGKILL/OOM/power
// loss, which the handler never sees).
bool lastRunCrashed();

// Byte offset into solero.log where that crash's section begins (0 if unknown), so a
// report can attach just the crash tail. Only meaningful when lastRunCrashed() is true.
qint64 crashLogOffset();

} // namespace solero
