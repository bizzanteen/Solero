#pragma once
#include "core/Types.h"
#include "tools/ToolCatalog.h"
#include <QString>
namespace solero {

namespace ToolSetup {

// Build a tool Executable from a catalog preset, the resolved primary exe path,
// and the Skyrim Proton wine prefix. Mirrors the inline logic the Set-Up-Tool
// wizard used: sets id/name/binary/args/runtime, the detected Proton version,
// the icon (preset's resource), and resolves each of the preset's extraActions
// to its secondary exe (case-insensitively) inside the primary exe's install
// dir. Output-mod wiring (isCapturingOutput/outputModId) is intentionally left
// to the caller, since that needs the active profile. Headless / GUI-free.
Executable buildExecutable(const ToolPreset& preset,
                           const QString& exePath,
                           const QString& winePrefix);

} // namespace ToolSetup
} // namespace solero
