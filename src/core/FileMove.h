#pragma once
#include <QString>

namespace solero {

// Move every file under `srcDir` into `destDir`, preserving the relative
// subfolder structure. Cross-filesystem safe: each file is QFile::rename'd
// first, falling back to copy + remove when rename fails (e.g. srcDir and
// destDir live on different mounts). Source subdirectories are removed once
// emptied, leaving `srcDir` itself in place but empty. Symlinks are skipped
// (the overwrite/capture files are always real files). `destDir` is created if
// it doesn't exist. Returns the number of files moved.
int moveTreeContents(const QString& srcDir, const QString& destDir);

} // namespace solero
