#include "ModInstaller.h"
#include "ArchiveTool.h"
#include "DataDirDetector.h"
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

} // namespace solero
