#pragma once
#include <QString>

namespace solero {
// `solero --rebuild [downloadsDir]`: re-stage missing mods from the downloads
// folder (or the given directory). Returns a process exit code.
int runRebuild(const QString& downloadsOverride = {});
}
