#pragma once
#include <QString>
#include <QSet>

namespace solero {

struct ModEntry; // fwd

// Sanitize a mod name into a filesystem-safe staging folder name.
// - Replaces / \ : * ? " < > | and ASCII control chars with '_'.
// - Collapses whitespace runs to single spaces; trims leading/trailing
//   whitespace and trailing dots.
// - Caps the result to 150 chars on a UTF-8 char boundary.
// - If empty or a Windows reserved name (CON, PRN, AUX, NUL, COM1-9, LPT1-9,
//   case-insensitive), prefixes '_'.
QString sanitizeStagingFolder(const QString& name);

// Resolve a unique staging folder name. If base.toLower() is in takenLower,
// appends " (2)", " (3)", … until free, trimming the base first if a suffixed
// name would exceed 150 chars. Returns the chosen (non-lowercased) value.
QString uniqueStagingFolder(const QString& base, const QSet<QString>& takenLower);

// Canonical resolver: the on-disk staging path for a mod.
QString stagingPathFor(const QString& stagingDir, const ModEntry& mod);

} // namespace solero
