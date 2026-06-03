#pragma once
#include "InstallLayout.h"
#include <QString>

namespace solero {

struct InstallResult {
    bool    success = false;
    QString modId;
    QString modName;
    bool    isFomod = false;
    QString errorMessage;
};

class ModInstaller {
public:
    static InstallResult installArchive(const QString& archivePath,
                                        const QString& stagingRoot);
private:
    static QString baseName(const QString& archivePath);
    static bool moveNormalized(const QString& extractDir,
                               const QString& modDir,
                               const InstallLayout& layout);
};

} // namespace solero
