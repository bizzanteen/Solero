#pragma once
#include "InstallLayout.h"
#include <QString>
#include <QStringList>
#include <QList>
#include <memory>
#include <functional>

class QTemporaryDir;

namespace solero {

struct FomodFile;

struct InstallResult {
    bool    success = false;
    QString modId;
    QString modName;
    bool    isFomod = false;
    QString errorMessage;
};

struct InstallPrep {
    std::shared_ptr<class QTemporaryDir> tempDir;
    QString extractDir;
    QString archivePath;
    QString modName;
    InstallLayout layout;
    QString fomodConfigPath;
    bool ok = false;
    QString errorMessage;
};

class ModInstaller {
public:
    static InstallResult installArchive(const QString& archivePath,
                                        const QString& stagingRoot);

    static InstallPrep prepare(const QString& archivePath,
                               const std::function<void(int)>& onProgress = {});
    static InstallResult stageSimple(InstallPrep& prep, const QString& stagingRoot,
                                     const QString& existingModId = {},
                                     const std::function<void(int)>& onProgress = {});
    static InstallResult stageFomod(InstallPrep& prep, const QString& stagingRoot,
                                    const QList<FomodFile>& files,
                                    const QString& existingModId = {},
                                    const std::function<void(int)>& onProgress = {});
    static void extractSubpaths(InstallPrep& prep, const QStringList& subpaths);
private:
    static QString baseName(const QString& archivePath);
    static bool moveNormalized(const QString& extractDir,
                               const QString& modDir,
                               const InstallLayout& layout);
    static QString resolveCaseInsensitive(const QString& base, const QString& rel);
    static bool copyDirInto(const QString& srcDir, const QString& dstDir);
    static bool extractFull(InstallPrep& prep, const std::function<void(int)>& onProgress = {});
};

} // namespace solero
