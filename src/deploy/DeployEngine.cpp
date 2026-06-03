#include "DeployEngine.h"
#include "Linker.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include <QDirIterator>
#include <QFile>
#include <QDir>
#include <QFileInfo>

namespace solero {

DeployEngine::DeployEngine(const QString& gameDir, const QString& stagingRoot)
    : m_gameDir(gameDir), m_stagingRoot(stagingRoot) {}

QString DeployEngine::recordPath(const QString& gameDir) {
    return gameDir + "/" + DeployRecord::recordFilename();
}

QString DeployEngine::conflictIndexPath(const QString& profileDir) {
    return profileDir + "/conflicts.json";
}

DeployResult DeployEngine::deploy(Profile& profile, DeployMode mode) {
    DeployResult result;
    Linker linker(mode);
    DeployRecord record;
    ConflictIndex conflicts;

    for (const auto& entry : profile.modList()) {
        if (entry.type != EntryType::Mod) continue;
        if (!entry.enabled) continue;
        deployMod(entry.id, m_gameDir, linker, record, conflicts);
    }

    QString pluginsTarget = m_gameDir + "/Data/Plugins.txt";
    QDir().mkpath(m_gameDir + "/Data");
    profile.pluginList().saveToFile(pluginsTarget);

    const QStringList inis = {
        profile.skyrimIniPath(),
        profile.skyrimPrefsPath(),
        profile.skyrimCustomPath()
    };
    const QStringList iniTargets = {
        m_gameDir + "/Skyrim.ini",
        m_gameDir + "/SkyrimPrefs.ini",
        m_gameDir + "/SkyrimCustom.ini"
    };
    for (int i = 0; i < inis.size(); ++i) {
        if (QFile::exists(inis[i])) {
            QFile::remove(iniTargets[i]);
            QFile::copy(inis[i], iniTargets[i]);
        }
    }

    record.saveToFile(recordPath(m_gameDir));
    conflicts.saveToFile(conflictIndexPath(profile.path()));

    result.success = true;
    result.conflicts = std::move(conflicts);
    result.filesDeployed = record.count();
    return result;
}

void DeployEngine::deployMod(const QString& modId,
                              const QString& gameDir,
                              const Linker& linker,
                              DeployRecord& record,
                              ConflictIndex& conflicts) {
    QString modRoot = m_stagingRoot + "/" + modId;
    if (!QDir(modRoot).exists()) return;

    QDirIterator it(modRoot, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString srcPath = it.next();
        QString relPath = srcPath.mid(modRoot.length() + 1);
        QString dstPath = gameDir + "/" + relPath;

        QString previousOwner = record.ownerOf(relPath);
        if (!previousOwner.isEmpty()) {
            conflicts.recordConflict(relPath, modId, previousOwner);
        } else {
            conflicts.setWinner(relPath, modId);
        }

        linker.deploy(srcPath, dstPath);
        record.add(relPath, modId);
    }
}

bool DeployEngine::undeploy(const QString& gameDir) {
    QString recPath = recordPath(gameDir);
    DeployRecord record = DeployRecord::loadFromFile(recPath);

    for (const auto& relPath : record.allPaths()) {
        QString fullPath = gameDir + "/" + relPath;
        QFile::remove(fullPath);
        QDir dir(QFileInfo(fullPath).path());
        while (dir.path() != gameDir && dir.isEmpty()) {
            QString parent = QFileInfo(dir.path()).path();
            dir.rmdir(".");
            dir = QDir(parent);
        }
    }

    QFile::remove(recPath);
    return true;
}

} // namespace solero
