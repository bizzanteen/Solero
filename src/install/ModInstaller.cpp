#include "ModInstaller.h"
#include "ArchiveTool.h"
#include "DataDirDetector.h"
#include "core/AppConfig.h"
#include "fomod/FomodTypes.h"
#include <QUuid>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>

namespace solero {

QString ModInstaller::baseName(const QString& archivePath) {
    QString base = QFileInfo(archivePath).completeBaseName();
    return base.isEmpty() ? "New Mod" : base;
}

// Template path for extraction temp dirs. The system temp dir (/tmp) is often a
// small tmpfs (e.g. 7 GB on this box) - large texture archives like Skyland AIO
// unpack to >10 GB and overflow it, surfacing as "Extraction failed.". Root the
// temp dir alongside the staging tree instead: it's on the big modding disk, so
// extraction has room and the final stage becomes a same-filesystem rename
// rather than a cross-device copy. Falls back to the system temp dir if no
// staging dir is configured yet.
QString ModInstaller::extractTmpTemplate() {
    const QString staging = AppConfig::instance().stagingDir();
    QString base = staging;
    if (base.isEmpty() || !QFileInfo(base).isDir())
        base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(base);
    return base + "/.solero-extract-XXXXXX";
}

bool ModInstaller::moveNormalized(const QString& extractDir,
                                  const QString& modDir,
                                  const InstallLayout& layout) {
    QDir extract(extractDir);
    QDirIterator it(extractDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString full = it.next();
        QString rel = extract.relativeFilePath(full);

        QString stripped = rel;
        for (int i = 0; i < layout.stripComponents; ++i) {
            int slash = stripped.indexOf('/');
            if (slash < 0) { stripped.clear(); break; }
            stripped = stripped.mid(slash + 1);
        }
        if (stripped.isEmpty()) continue;

        QString target = layout.wrapInData ? ("Data/" + stripped) : stripped;
        QString dst = modDir + "/" + target;
        QDir().mkpath(QFileInfo(dst).path());
        QFile::remove(dst);
        if (!QFile::rename(full, dst)) {
            if (!QFile::copy(full, dst)) return false;
        }
    }
    return true;
}

InstallResult ModInstaller::installArchive(const QString& archivePath,
                                           const QString& stagingRoot) {
    InstallResult r;
    if (!ArchiveTool::sevenZipAvailable()) { r.errorMessage = "7z is not installed."; return r; }

    bool listed = false;
    QStringList entries = ArchiveTool::listEntries(archivePath, &listed);
    if (!listed || entries.isEmpty()) { r.errorMessage = "Could not read the archive (is it valid?)."; return r; }

    InstallLayout layout = DataDirDetector::detect(entries);
    r.isFomod = layout.isFomod;

    QTemporaryDir extractTmp(extractTmpTemplate());
    if (!extractTmp.isValid()) { r.errorMessage = "No temp dir."; return r; }
    if (!ArchiveTool::extract(archivePath, extractTmp.path())) { r.errorMessage = "Extraction failed."; return r; }

    r.modId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.modName = baseName(archivePath);
    QString modDir = stagingRoot + "/" + r.modId;
    QDir().mkpath(modDir);

    if (!moveNormalized(extractTmp.path(), modDir, layout)) {
        r.errorMessage = "Failed to stage files.";
        QDir(modDir).removeRecursively();
        return r;
    }
    r.success = true;
    return r;
}

InstallPrep ModInstaller::prepare(const QString& archivePath,
                                  const std::function<void(int)>& onProgress) {
    InstallPrep prep;
    if (!ArchiveTool::sevenZipAvailable()) { prep.errorMessage = "7z is not installed."; return prep; }
    bool listed = false;
    QStringList entries = ArchiveTool::listEntries(archivePath, &listed);
    if (!listed || entries.isEmpty()) { prep.errorMessage = "Could not read the archive."; return prep; }

    prep.layout = DataDirDetector::detect(entries);
    prep.modName = baseName(archivePath);
    prep.archivePath = archivePath;
    prep.tempDir = std::make_shared<QTemporaryDir>(extractTmpTemplate());
    if (!prep.tempDir->isValid()) { prep.errorMessage = "No temp dir."; return prep; }
    prep.extractDir = prep.tempDir->path();
    if (prep.layout.isFomod) {
        if (ArchiveTool::isSolid(archivePath)) {
            // Solid archive: partial extract is no cheaper than full - extract once.
            if (!ArchiveTool::extract(archivePath, prep.extractDir, onProgress)) {
                prep.errorMessage = "Extraction failed."; return prep;
            }
            prep.fullyExtracted = true;
        } else {
            // Non-solid: cheap responsive partial extract of just fomod/.
            ArchiveTool::extractPaths(archivePath, prep.extractDir, {"fomod"}, true, onProgress);
        }
    } else {
        if (!ArchiveTool::extract(archivePath, prep.extractDir, onProgress)) { prep.errorMessage = "Extraction failed."; return prep; }
        prep.fullyExtracted = true;
    }

    if (prep.layout.isFomod) {
        auto locate = [&]() {
            QDirIterator it(prep.extractDir, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QString f = it.next();
                if (f.endsWith("/ModuleConfig.xml", Qt::CaseInsensitive)) {
                    if (QFileInfo(f).dir().dirName().compare("fomod", Qt::CaseInsensitive) == 0) {
                        prep.fomodConfigPath = f; return;
                    }
                }
            }
        };
        locate();
        if (prep.fomodConfigPath.isEmpty()) {
            // Fast path missed it; fall back to a full extraction.
            if (!ArchiveTool::extract(archivePath, prep.extractDir)) { prep.errorMessage = "Extraction failed."; return prep; }
            prep.fullyExtracted = true;
            locate();
        }
    }
    prep.ok = true;
    return prep;
}

void ModInstaller::extractSubpaths(InstallPrep& prep, const QStringList& subpaths,
                                   const std::function<void(int)>& onProgress) {
    if (subpaths.isEmpty() || prep.archivePath.isEmpty()) return;
    ArchiveTool::extractPaths(prep.archivePath, prep.extractDir, subpaths, true, onProgress);
}

bool ModInstaller::extractFull(InstallPrep& prep, const std::function<void(int)>& onProgress) {
    bool ok = ArchiveTool::extract(prep.archivePath, prep.extractDir, onProgress);
    if (ok) prep.fullyExtracted = true;
    return ok;
}

InstallResult ModInstaller::stageSimple(InstallPrep& prep, const QString& stagingRoot,
                                        const QString& existingModId,
                                        const std::function<void(int)>& onProgress) {
    InstallResult r;
    if (!prep.ok) { r.errorMessage = prep.errorMessage; return r; }
    if (prep.layout.isFomod && !prep.fullyExtracted) extractFull(prep, onProgress); // wizard only extracted fomod/; rare parse-fail fallback needs all files
    r.modId = existingModId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existingModId;
    r.modName = prep.modName;
    r.isFomod = prep.layout.isFomod;
    QString modDir = stagingRoot + "/" + r.modId;
    if (!existingModId.isEmpty()) {
        // Reinstall: wipe the previous staged files (keep the dir).
        QDir md(modDir);
        for (const QString& e : md.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString full = modDir + "/" + e;
            if (QFileInfo(full).isDir()) QDir(full).removeRecursively();
            else QFile::remove(full);
        }
    }
    QDir().mkpath(modDir);
    if (!moveNormalized(prep.extractDir, modDir, prep.layout)) {
        r.errorMessage = "Failed to stage files."; QDir(modDir).removeRecursively(); return r;
    }
    r.success = true;
    return r;
}

InstallResult ModInstaller::stageFomod(InstallPrep& prep, const QString& stagingRoot,
                                       const QList<FomodFile>& files,
                                       const QString& existingModId,
                                       const std::function<void(int)>& onProgress) {
    InstallResult r;
    if (!prep.ok) { r.errorMessage = prep.errorMessage; return r; }
    if (!prep.fullyExtracted) extractFull(prep, onProgress); // wizard only extracted fomod/; now get the rest
    r.modId = existingModId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existingModId;
    r.modName = prep.modName;
    r.isFomod = true;
    QString modDir = stagingRoot + "/" + r.modId;
    if (!existingModId.isEmpty()) {
        // Reinstall: wipe the previous staged files (keep the dir).
        QDir md(modDir);
        for (const QString& e : md.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString full = modDir + "/" + e;
            if (QFileInfo(full).isDir()) QDir(full).removeRecursively();
            else QFile::remove(full);
        }
    }

    QString fomodDir = QFileInfo(prep.fomodConfigPath).dir().path(); // .../fomod
    QString fomodBase = QFileInfo(fomodDir).dir().path();            // parent of fomod
    copyFomodFiles(fomodBase, files, modDir);
    r.success = true;
    return r;
}

void ModInstaller::copyFomodFiles(const QString& fomodBase, const QList<FomodFile>& files,
                                  const QString& modDir) {
    // FOMOD spec: when two sources write the same destination, the one with the
    // higher priority wins. We copy last-writer-wins, so stable-sort ASCENDING
    // by priority - higher priority is copied last and overwrites lower.
    QList<FomodFile> ordered = files;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const FomodFile& a, const FomodFile& b){ return a.priority < b.priority; });

    for (const FomodFile& f : ordered) {
        QString src = fomodBase + "/" + f.source;
        if (!QFileInfo::exists(src)) {
            QString found = resolveCaseInsensitive(fomodBase, f.source);
            if (!found.isEmpty()) src = found;
        }
        bool isDir = f.isFolder || QFileInfo(src).isDir();
        if (isDir) {
            // Folder: copy CONTENTS into Data/<destination> (empty destination => Data root).
            QString dst = modDir + "/Data" + (f.destination.isEmpty() ? QString() : ("/" + f.destination));
            copyDirInto(src, dst);
        } else {
            // File: destination defaults to source path (relative to Data).
            QString destRel = f.destination.isEmpty() ? f.source : f.destination;
            QString dst = modDir + "/Data/" + destRel;
            QDir().mkpath(QFileInfo(dst).path());
            QFile::remove(dst);
            QFile::copy(src, dst);
        }
    }
}

QString ModInstaller::fomodBaseFor(const QString& extractDir) {
    QDirIterator it(extractDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString f = it.next();
        if (f.endsWith("/ModuleConfig.xml", Qt::CaseInsensitive)
            && QFileInfo(f).dir().dirName().compare("fomod", Qt::CaseInsensitive) == 0) {
            QString fomodDir = QFileInfo(f).dir().path();   // .../fomod
            return QFileInfo(fomodDir).dir().path();         // parent of fomod
        }
    }
    return extractDir;
}

bool ModInstaller::installOptionFiles(const QString& archivePath, const QString& modDir,
                                      const QList<FomodFile>& files,
                                      const std::function<void(int)>& onProgress) {
    if (files.isEmpty()) return true;
    if (!ArchiveTool::sevenZipAvailable()) return false;
    QTemporaryDir tmp(extractTmpTemplate());
    if (!tmp.isValid()) return false;
    // Extract the whole archive so all candidate source files are available
    // (option sources may be solid/scattered); the temp dir auto-cleans.
    if (!ArchiveTool::extract(archivePath, tmp.path(), onProgress)) return false;
    QString fomodBase = fomodBaseFor(tmp.path());
    copyFomodFiles(fomodBase, files, modDir);
    return true;
}

QString ModInstaller::resolveCaseInsensitive(const QString& base, const QString& rel) {
    QStringList parts = rel.split('/', Qt::SkipEmptyParts);
    QString cur = base;
    for (const QString& part : parts) {
        QDir d(cur);
        QString match;
        for (const QString& e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
            if (e.compare(part, Qt::CaseInsensitive) == 0) { match = e; break; }
        if (match.isEmpty()) return {};
        cur = cur + "/" + match;
    }
    return cur;
}

bool ModInstaller::copyDirInto(const QString& srcDir, const QString& dstDir) {
    QDir().mkpath(dstDir);
    QDirIterator it(srcDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString f = it.next();
        QString rel = QDir(srcDir).relativeFilePath(f);
        QString dst = dstDir + "/" + rel;
        QDir().mkpath(QFileInfo(dst).path());
        QFile::remove(dst);
        if (!QFile::copy(f, dst)) return false;
    }
    return true;
}

} // namespace solero
