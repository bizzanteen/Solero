#pragma once
#include "core/Types.h"
#include "tools/ToolCatalog.h"
#include <QString>
namespace solero {

namespace ToolSetup {

// Build a tool Executable from a catalog preset, the resolved exe and the wine
// prefix, resolving each extraAction to its secondary exe in the same install
// dir. Output-mod wiring is left to the caller, since it needs the profile.
Executable buildExecutable(const ToolPreset& preset,
                           const QString& exePath,
                           const QString& winePrefix);

} // namespace ToolSetup
} // namespace solero
