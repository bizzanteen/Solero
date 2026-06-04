#pragma once
#include "InstallLayout.h"
#include <QString>
#include <QList>
#include <memory>

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

    static InstallPrep prepare(const QString& archivePath);
    static InstallResult stageSimple(InstallPrep& prep, const QString& stagingRoot);
    static InstallResult stageFomod(InstallPrep& prep, const QString& stagingRoot,
                                    const QList<FomodFile>& files);
private:
    static QString baseName(const QString& archivePath);
    static bool moveNormalized(const QString& extractDir,
                               const QString& modDir,
                               const InstallLayout& layout);
    static QString resolveCaseInsensitive(const QString& base, const QString& rel);
    static bool copyDirInto(const QString& srcDir, const QString& dstDir);
    static bool extractFull(InstallPrep& prep);
};

} // namespace solero
