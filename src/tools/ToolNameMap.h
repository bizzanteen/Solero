#pragma once
#include "core/Types.h"
#include <QString>
#include <QList>
namespace solero {

// Map a discovered MO2 tool (from ModOrganizer.ini [customExecutables]) to a
// ToolCatalog preset id, matching case-insensitively on the configured title OR
// the binary's basename. Returns "" when no preset matches (the caller then sets
// the tool up as a custom executable). Known mappings:
//   DynDOLOD / TexGen      -> dyndolod
//   SSEEdit / xEdit        -> xedit
//   Nemesis                -> nemesis
//   Pandora                -> pandora
//   Synthesis              -> synthesis
//   ESLifier               -> eslifier
//   PGPatcher              -> pgpatcher
//   Radium                 -> radium
QString presetIdForToolName(const QString& name, const QString& binary);

// Find an existing "<tool> Output"-style mod in `mods` for a tool named
// `toolName`, without creating one (used to wire UNMAPPED custom tools to a
// shipped output mod when the list already has one). Match rules, in order:
//   1. exact "<toolName> Output" (case-insensitive),
//   2. a mod that contains the tool's keyword and looks like an output target
//      (name marked output / contains "Output"), excluding "Resource" mods.
// Returns the matched mod's id, or "" if none. Pure / headless-testable.
QString findOutputModId(const QList<ModEntry>& mods, const QString& toolName);

}
