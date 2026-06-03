#pragma once
#include "InstallLayout.h"
#include <QStringList>

namespace solero {

class DataDirDetector {
public:
    // paths: archive-relative, '/'-separated, no leading slash.
    static InstallLayout detect(const QStringList& paths);

private:
    static bool isDataMarkerDir(const QString& name);
    static bool isPluginFile(const QString& name);
    static QString topComponent(const QString& path);
};

} // namespace solero
