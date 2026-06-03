#include "DeployEngine.h"
#include "Linker.h"
#include "loot/LootSorter.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include <QDirIterator>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

namespace solero {

DeployEngine::DeployEngine(const QString& gameDir, const QString& stagingRoot)
    : m_gameDir(QDir::cleanPath(gameDir)), m_stagingRoot(QDir::cleanPath(stagingRoot)) {}

QString DeployEngine::recordPath(const QString& gameDir) {
    return gameDir + "/" + DeployRecord::recordFilename();
}

QString DeployEngine::conflictIndexPath(const QString& profileDir) {
    return profileDir + "/conflicts.json";
}

DeployResult DeployEngine::deploy(Profile& profile, DeployMode mode) {
    DeployResult result;

    // Clean up any previous deployment first to avoid orphaned files
    undeploy(m_gameDir);

    Linker linker(mode);
    DeployRecord record;
    ConflictIndex conflicts;

    for (const auto& entry : profile.modList()) {
        if (entry.type != EntryType::Mod) continue;
        if (!entry.enabled) continue;
        deployMod(entry.id, m_gameDir, linker, record, conflicts);
    }

    // Sort plugins with LOOT (if enabled) before writing plugins.txt. The mod
    // files must already be deployed above so LOOT can read the plugin headers.
    if (m_lootEnabled) {
        auto sortResult = LootSorter::sort(
            profile.pluginList(),
            m_gameDir,
            m_userlistPath.isEmpty() ? profile.lootUserlistPath() : m_userlistPath);
        if (!sortResult.success)
            qWarning() << "LOOT sort failed (deploying with current order):"
                       << sortResult.errorMessage;
        profile.save(); // persist the sorted order back to the profile
    }

    // Plugins.txt belongs in the game's local appdata folder (inside the Proton
    // prefix), where Skyrim actually reads it. Fall back to Data/ if unknown.
    QString localAppData = AppConfig::instance().localAppDataDir();
    QString pluginsDir = localAppData.isEmpty() ? (m_gameDir + "/Data") : localAppData;
    QDir().mkpath(pluginsDir);
    profile.pluginList().saveToFile(pluginsDir + "/Plugins.txt");

    // INIs are managed directly as a single live game set (edited via the BethINI
    // tab), not copied per-profile on deploy.

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
    const QString normGameDir = QDir::cleanPath(gameDir);
    QString recPath = recordPath(normGameDir);
    DeployRecord record = DeployRecord::loadFromFile(recPath);

    for (const auto& relPath : record.allPaths()) {
        QString fullPath = normGameDir + "/" + relPath;
        QFile::remove(fullPath);
        QDir dir(QFileInfo(fullPath).path());
        while (QDir::cleanPath(dir.path()) != normGameDir && dir.isEmpty()) {
            QString parent = QFileInfo(dir.path()).path();
            dir.rmdir(".");
            dir = QDir(parent);
        }
    }

    QFile::remove(recPath);
    return true;
}

} // namespace solero
