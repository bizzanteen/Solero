#pragma once
#include <QString>

namespace solero {

// Move everything under srcDir into destDir, keeping the subfolder structure.
// Rename first, copy+remove across mounts. Emptied source subdirs are removed,
// srcDir itself stays. Symlinks skipped. Returns the number of files moved.
int moveTreeContents(const QString& srcDir, const QString& destDir);

} // namespace solero
