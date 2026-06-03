#include "ModInstaller.h"
#include "ArchiveTool.h"
#include "DataDirDetector.h"
#include "fomod/FomodTypes.h"
#include <QUuid>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace solero {

QString ModInstaller::baseName(const QString& archivePath) {
    QString base = QFileInfo(archivePath).completeBaseName();
    return base.isEmpty() ? "New Mod" : base;
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

    QTemporaryDir extractTmp;
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

InstallPrep ModInstaller::prepare(const QString& archivePath) {
    InstallPrep prep;
    if (!ArchiveTool::sevenZipAvailable()) { prep.errorMessage = "7z is not installed."; return prep; }
    bool listed = false;
    QStringList entries = ArchiveTool::listEntries(archivePath, &listed);
    if (!listed || entries.isEmpty()) { prep.errorMessage = "Could not read the archive."; return prep; }

    prep.layout = DataDirDetector::detect(entries);
    prep.modName = baseName(archivePath);
    prep.tempDir = std::make_shared<QTemporaryDir>();
    if (!prep.tempDir->isValid()) { prep.errorMessage = "No temp dir."; return prep; }
    prep.extractDir = prep.tempDir->path();
    if (!ArchiveTool::extract(archivePath, prep.extractDir)) { prep.errorMessage = "Extraction failed."; return prep; }

    if (prep.layout.isFomod) {
        QDirIterator it(prep.extractDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString f = it.next();
            if (f.endsWith("/ModuleConfig.xml", Qt::CaseInsensitive)) {
                if (QFileInfo(f).dir().dirName().compare("fomod", Qt::CaseInsensitive) == 0) {
                    prep.fomodConfigPath = f; break;
                }
            }
        }
    }
    prep.ok = true;
    return prep;
}

InstallResult ModInstaller::stageSimple(InstallPrep& prep, const QString& stagingRoot) {
    InstallResult r;
    if (!prep.ok) { r.errorMessage = prep.errorMessage; return r; }
    r.modId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.modName = prep.modName;
    r.isFomod = prep.layout.isFomod;
    QString modDir = stagingRoot + "/" + r.modId;
    QDir().mkpath(modDir);
    if (!moveNormalized(prep.extractDir, modDir, prep.layout)) {
        r.errorMessage = "Failed to stage files."; QDir(modDir).removeRecursively(); return r;
    }
    r.success = true;
    return r;
}

InstallResult ModInstaller::stageFomod(InstallPrep& prep, const QString& stagingRoot,
                                       const QList<FomodFile>& files) {
    InstallResult r;
    if (!prep.ok) { r.errorMessage = prep.errorMessage; return r; }
    r.modId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.modName = prep.modName;
    r.isFomod = true;
    QString modDir = stagingRoot + "/" + r.modId;

    QString fomodDir = QFileInfo(prep.fomodConfigPath).dir().path(); // .../fomod
    QString fomodBase = QFileInfo(fomodDir).dir().path();            // parent of fomod

    for (const FomodFile& f : files) {
        QString src = fomodBase + "/" + f.source;
        if (!QFileInfo::exists(src)) {
            QString found = resolveCaseInsensitive(fomodBase, f.source);
            if (!found.isEmpty()) src = found;
        }
        QString destRel = f.destination.isEmpty() ? f.source : f.destination;
        QString dst = modDir + "/Data/" + destRel;
        if (f.isFolder || QFileInfo(src).isDir()) {
            copyDirInto(src, dst);
        } else {
            QDir().mkpath(QFileInfo(dst).path());
            QFile::remove(dst);
            QFile::copy(src, dst);
        }
    }
    r.success = true;
    return r;
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
