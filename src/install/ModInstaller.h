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
    // Module root the FOMOD's image/source paths are relative to - i.e. the
    // parent of the `fomod` folder. Differs from extractDir when the archive
    // wraps everything in a top-level folder (e.g. "Skyland AIO/fomod/..."),
    // so the wizard must resolve images against this, not extractDir.
    QString fomodBase;
    bool fullyExtracted = false;
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
    static void extractSubpaths(InstallPrep& prep, const QStringList& subpaths,
                                const std::function<void(int)>& onProgress = {});

    // Retroactively install a subset of FOMOD option files into an already-staged
    // mod without wiping it (used by the Patch Wizard). Extracts the needed source
    // files from archivePath and copies source->destination into modDir/Data.
    static bool installOptionFiles(const QString& archivePath, const QString& modDir,
                                   const QList<FomodFile>& files,
                                   const std::function<void(int)>& onProgress = {});
private:
    static QString baseName(const QString& archivePath);
    // QTemporaryDir template rooted on the staging disk (not the small /tmp
    // tmpfs) so multi-GB archives have room to extract. See the .cpp for why.
    static QString extractTmpTemplate();
    static bool moveNormalized(const QString& extractDir,
                               const QString& modDir,
                               const InstallLayout& layout);
    static QString resolveCaseInsensitive(const QString& base, const QString& rel);
    static bool copyDirInto(const QString& srcDir, const QString& dstDir);
    static bool extractFull(InstallPrep& prep, const std::function<void(int)>& onProgress = {});
    // Copy FOMOD source->destination entries (priority-ordered, last-writer-wins)
    // into modDir/Data. fomodBase is the archive root that the FOMOD `source`
    // paths are relative to (the parent of the `fomod` folder).
    static void copyFomodFiles(const QString& fomodBase, const QList<FomodFile>& files,
                               const QString& modDir);
    // Locate the FOMOD config inside an extracted tree and return the archive root
    // (parent of the `fomod` folder). Falls back to extractDir if not found.
    static QString fomodBaseFor(const QString& extractDir);
};

} // namespace solero
