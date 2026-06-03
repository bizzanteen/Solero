#pragma once
#include "DeployMode.h"
#include "DeployRecord.h"
#include "ConflictIndex.h"
#include "Linker.h"
#include <QString>

namespace solero { class Profile; }

namespace solero {

struct DeployResult {
    bool success = false;
    QString errorMessage;
    ConflictIndex conflicts;
    int filesDeployed = 0;
};

class DeployEngine {
public:
    DeployEngine(const QString& gameDir, const QString& stagingRoot);

    DeployResult deploy(Profile& profile, DeployMode mode = DeployMode::HardLink);
    bool undeploy(const QString& gameDir);

    static QString recordPath(const QString& gameDir);
    static QString conflictIndexPath(const QString& profileDir);

private:
    QString m_gameDir;
    QString m_stagingRoot;

    void deployMod(const QString& modId,
                   const QString& gameDir,
                   const Linker& linker,
                   DeployRecord& record,
                   ConflictIndex& conflicts);
};

} // namespace solero
