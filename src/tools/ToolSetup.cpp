#include "ToolSetup.h"
#include "core/AppConfig.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace solero {
namespace ToolSetup {

Executable buildExecutable(const ToolPreset& preset,
                           const QString& exePath,
                           const QString& winePrefix) {
    Executable e;
    e.id = preset.id;
    e.name = preset.name;
    e.binaryPath = exePath;
    e.arguments = preset.args;
    e.runtime = preset.proton ? RuntimeType::Proton : RuntimeType::Native;
    e.iconPath = preset.iconResource;
    e.protonVersion = QFileInfo(AppConfig::instance().detectProtonDir()).fileName();
    e.winePrefix = winePrefix;
    e.runThroughDeployer = false;

    // Build extra actions: resolve each secondary exe in the same install dir,
    // case-insensitively (the install may have a differently-cased filename).
    const QString instDir = QFileInfo(exePath).path();
    for (const auto& pa : preset.extraActions) {
        QString actExe = instDir + "/" + pa.exeRelPath;
        if (!QFile::exists(actExe)) { // case-insensitive shallow search
            for (const QString& f : QDir(instDir).entryList(QDir::Files))
                if (f.compare(pa.exeRelPath, Qt::CaseInsensitive) == 0) {
                    actExe = instDir + "/" + f;
                    break;
                }
        }
        ToolAction a;
        a.label = pa.label;
        a.binaryPath = actExe;
        a.arguments = pa.args;
        a.outputModId = QString();
        e.extraActions.append(a);
    }
    return e;
}

} // namespace ToolSetup
} // namespace solero
