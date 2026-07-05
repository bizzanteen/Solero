#include "ModInstaller.h"
#include "ArchiveTool.h"
#include "DataDirDetector.h"
#include "core/AppConfig.h"
#include "core/Log.h"
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

        // Never stage FOMOD installer metadata into the mod: a top-level "fomod"
        // path component (case-insensitive) holds ModuleConfig.xml/info.xml and
        // installer images, which would otherwise leak into the game's Data dir
        // (e.g. on the parse-failure fallback that stages all files). Match a
        // leading "fomod/" in the stripped path.
        {
            const int slash = stripped.indexOf('/');
            const QString topComp = slash < 0 ? stripped : stripped.left(slash);
            if (topComp.compare("fomod", Qt::CaseInsensitive) == 0) continue;
        }

        QString target = layout.wrapInData ? ("Data/" + stripped) : stripped;
        QString dst = modDir + "/" + target;
        QDir().mkpath(QFileInfo(dst).path());
        QFile::remove(dst);
        qCDebug(lcInstall) << "normalize:" << full << "->" << dst;
        if (!QFile::rename(full, dst)) {
            if (!QFile::copy(full, dst)) { qCWarning(lcInstall) << "moveNormalized: copy failed" << full << "->" << dst; return false; }
        }
    }
    return true;
}

InstallResult ModInstaller::installArchive(const QString& archivePath,
                                           const QString& stagingRoot) {
    InstallResult r;
    qCInfo(lcInstall) << "installArchive start:" << archivePath << "-> staging" << stagingRoot;
    if (!ArchiveTool::sevenZipAvailable()) { r.errorMessage = "7z is not installed."; qCWarning(lcInstall) << "installArchive: 7z not installed"; return r; }

    bool listed = false;
    QStringList entries = ArchiveTool::listEntries(archivePath, &listed);
    if (!listed || entries.isEmpty()) { r.errorMessage = "Could not read the archive (is it valid?)."; qCWarning(lcInstall) << "installArchive: could not read archive" << archivePath; return r; }

    InstallLayout layout = DataDirDetector::detect(entries);
    r.isFomod = layout.isFomod;
    qCInfo(lcInstall) << "layout: isFomod" << layout.isFomod << "wrapInData" << layout.wrapInData
                      << "stripComponents" << layout.stripComponents << "(" << entries.size() << "entries )";

    QTemporaryDir extractTmp(extractTmpTemplate());
    if (!extractTmp.isValid()) { r.errorMessage = "Could not create a temporary extraction folder. Make sure the staging directory exists and the disk has enough free space."; qCWarning(lcInstall) << "installArchive: could not create temp extract dir"; return r; }
    if (!ArchiveTool::extract(archivePath, extractTmp.path())) { r.errorMessage = "Could not extract the archive - it may be corrupt, an unsupported format, or too large for the available space. Try re-downloading the mod."; qCWarning(lcInstall) << "installArchive: extract failed" << archivePath; return r; }

    r.modId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.modName = baseName(archivePath);
    QString modDir = stagingRoot + "/" + r.modId;
    QDir().mkpath(modDir);

    if (!moveNormalized(extractTmp.path(), modDir, layout)) {
        r.errorMessage = "The archive extracted but files could not be moved into the staging folder. Check that the staging directory is writable and has enough free space.";
        qCWarning(lcInstall) << "installArchive: moveNormalized failed for" << r.modName;
        QDir(modDir).removeRecursively();
        return r;
    }
    r.success = true;
    qCInfo(lcInstall) << "installArchive done: mod" << r.modName << "-> " << modDir << "isFomod" << r.isFomod;
    return r;
}

InstallPrep ModInstaller::prepare(const QString& archivePath,
                                  const std::function<void(int)>& onProgress) {
    InstallPrep prep;
    qCInfo(lcInstall) << "prepare start:" << archivePath;
    if (!ArchiveTool::sevenZipAvailable()) { prep.errorMessage = "7z is not installed."; qCWarning(lcInstall) << "prepare: 7z not installed"; return prep; }
    bool listed = false;
    QStringList entries = ArchiveTool::listEntries(archivePath, &listed);
    if (!listed || entries.isEmpty()) { prep.errorMessage = "Could not read the archive."; qCWarning(lcInstall) << "prepare: could not read archive" << archivePath; return prep; }

    prep.layout = DataDirDetector::detect(entries);
    qCInfo(lcInstall) << "prepare layout: isFomod" << prep.layout.isFomod << "wrapInData" << prep.layout.wrapInData
                      << "stripComponents" << prep.layout.stripComponents << "(" << entries.size() << "entries )";
    prep.modName = baseName(archivePath);
    prep.archivePath = archivePath;
    prep.tempDir = std::make_shared<QTemporaryDir>(extractTmpTemplate());
    if (!prep.tempDir->isValid()) { prep.errorMessage = "Could not create a temporary extraction folder. Make sure the staging directory exists and the disk has enough free space."; qCWarning(lcInstall) << "prepare: could not create temp extract dir"; return prep; }
    prep.extractDir = prep.tempDir->path();
    if (prep.layout.isFomod) {
        if (ArchiveTool::isSolid(archivePath)) {
            // Solid archive: partial extract is no cheaper than full - extract once.
            if (!ArchiveTool::extract(archivePath, prep.extractDir, onProgress)) {
                prep.errorMessage = "Could not extract the archive - it may be corrupt, an unsupported format, or too large for the available space. Try re-downloading the mod."; qCWarning(lcInstall) << "prepare: solid FOMOD extract failed" << archivePath; return prep;
            }
            prep.fullyExtracted = true;
        } else {
            // Non-solid: cheap responsive partial extract of just fomod/.
            ArchiveTool::extractPaths(archivePath, prep.extractDir, {"fomod"}, true, onProgress);
        }
    } else {
        if (!ArchiveTool::extract(archivePath, prep.extractDir, onProgress)) { prep.errorMessage = "Could not extract the archive - it may be corrupt, an unsupported format, or too large for the available space. Try re-downloading the mod."; qCWarning(lcInstall) << "prepare: extract failed" << archivePath; return prep; }
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
            qCWarning(lcInstall) << "prepare: ModuleConfig.xml not found in partial extract, full-extracting" << archivePath;
            if (!ArchiveTool::extract(archivePath, prep.extractDir)) { prep.errorMessage = "Could not extract the archive - it may be corrupt, an unsupported format, or too large for the available space. Try re-downloading the mod."; qCWarning(lcInstall) << "prepare: fallback full extract failed" << archivePath; return prep; }
            prep.fullyExtracted = true;
            locate();
        }
        // The module root that the FOMOD's image/source paths are relative to is
        // the PARENT of the `fomod` folder - which is extractDir for a flat
        // archive but a wrapper subdir (e.g. "Skyland AIO/") when the archive
        // nests everything one level deep. Derive it from the located config so
        // the wizard resolves images against the right base.
        if (!prep.fomodConfigPath.isEmpty()) {
            QDir fd(QFileInfo(prep.fomodConfigPath).absolutePath()); // .../fomod
            fd.cdUp();                                               // module root
            prep.fomodBase = fd.absolutePath();
        }
    }
    if (prep.fomodBase.isEmpty()) prep.fomodBase = prep.extractDir;
    prep.ok = true;
    qCInfo(lcInstall) << "prepare done:" << prep.modName << "isFomod" << prep.layout.isFomod
                      << "fomodConfig" << (prep.fomodConfigPath.isEmpty() ? QStringLiteral("(none)") : prep.fomodConfigPath);
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
                                        const std::function<void(int)>& onProgress,
                                        const QString& stagingFolderOverride) {
    InstallResult r;
    qCInfo(lcInstall) << "stageSimple start:" << prep.modName << "-> staging" << stagingRoot;
    if (!prep.ok) { r.errorMessage = prep.errorMessage; qCWarning(lcInstall) << "stageSimple: prep not ok -" << prep.errorMessage; return r; }
    if (prep.layout.isFomod && !prep.fullyExtracted) extractFull(prep, onProgress); // wizard only extracted fomod/; rare parse-fail fallback needs all files
    r.modId = existingModId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existingModId;
    r.modName = prep.modName;
    r.isFomod = prep.layout.isFomod;
    // Replace/Reinstall: write into the existing mod's migrated staging folder when
    // provided, else fall back to the UUID dir (new installs).
    QString modDir = stagingFolderOverride.isEmpty()
        ? (stagingRoot + "/" + r.modId) : stagingFolderOverride;
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
        r.errorMessage = "The archive extracted but files could not be moved into the staging folder. Check that the staging directory is writable and has enough free space."; qCWarning(lcInstall) << "stageSimple: moveNormalized failed for" << r.modName; QDir(modDir).removeRecursively(); return r;
    }
    r.success = true;
    qCInfo(lcInstall) << "stageSimple done:" << r.modName << "->" << modDir;
    return r;
}

InstallResult ModInstaller::stageFomod(InstallPrep& prep, const QString& stagingRoot,
                                       const QList<FomodFile>& files,
                                       const QString& existingModId,
                                       const std::function<void(int)>& onProgress,
                                       const QString& stagingFolderOverride) {
    InstallResult r;
    qCInfo(lcInstall) << "stageFomod start:" << prep.modName << "with" << files.size() << "selected files -> staging" << stagingRoot;
    if (!prep.ok) { r.errorMessage = prep.errorMessage; qCWarning(lcInstall) << "stageFomod: prep not ok -" << prep.errorMessage; return r; }
    if (!prep.fullyExtracted) extractFull(prep, onProgress); // wizard only extracted fomod/; now get the rest
    r.modId = existingModId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : existingModId;
    r.modName = prep.modName;
    r.isFomod = true;
    // Replace/Reinstall: write into the existing mod's migrated staging folder when
    // provided, else fall back to the UUID dir (new installs).
    QString modDir = stagingFolderOverride.isEmpty()
        ? (stagingRoot + "/" + r.modId) : stagingFolderOverride;
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
    r.success = true; // note: copyFomodFiles swallows per-file failures; success is unconditional
    qCInfo(lcInstall) << "stageFomod done:" << r.modName << "->" << modDir << "(" << files.size() << "file entries )";
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
        if (!QFileInfo::exists(src)) qCWarning(lcInstall) << "copyFomodFiles: source missing" << src;
        bool isDir = f.isFolder || QFileInfo(src).isDir();
        if (isDir) {
            // Folder: copy CONTENTS into Data/<destination> (empty destination => Data root).
            QString dst = modDir + "/Data" + (f.destination.isEmpty() ? QString() : ("/" + f.destination));
            qCDebug(lcInstall) << "copyFomodFiles dir:" << src << "->" << dst;
            if (!copyDirInto(src, dst)) qCWarning(lcInstall) << "copyFomodFiles: copyDirInto failed" << src << "->" << dst;
        } else {
            // File: destination defaults to source path (relative to Data).
            QString destRel = f.destination.isEmpty() ? f.source : f.destination;
            QString dst = modDir + "/Data/" + destRel;
            QDir().mkpath(QFileInfo(dst).path());
            QFile::remove(dst);
            qCDebug(lcInstall) << "copyFomodFiles file:" << src << "->" << dst;
            if (!QFile::copy(src, dst)) qCWarning(lcInstall) << "copyFomodFiles: copy failed" << src << "->" << dst;
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
    qCInfo(lcInstall) << "installOptionFiles:" << files.size() << "files from" << archivePath << "->" << modDir;
    if (!ArchiveTool::sevenZipAvailable()) { qCWarning(lcInstall) << "installOptionFiles: 7z unavailable"; return false; }
    QTemporaryDir tmp(extractTmpTemplate());
    if (!tmp.isValid()) { qCWarning(lcInstall) << "installOptionFiles: could not create temp extract dir"; return false; }
    // Extract the whole archive so all candidate source files are available
    // (option sources may be solid/scattered); the temp dir auto-cleans.
    if (!ArchiveTool::extract(archivePath, tmp.path(), onProgress)) { qCWarning(lcInstall) << "installOptionFiles: extract failed" << archivePath; return false; }
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
        qCDebug(lcInstall) << "copyDirInto:" << f << "->" << dst;
        if (!QFile::copy(f, dst)) { qCWarning(lcInstall) << "copyDirInto: copy failed" << f << "->" << dst; return false; }
    }
    return true;
}

} // namespace solero
