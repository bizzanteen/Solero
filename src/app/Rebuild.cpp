#include "Rebuild.h"
#include "core/AppConfig.h"
#include "core/Log.h"
#include "core/Profile.h"
#include "core/ProfileManager.h"
#include "core/StagingFolder.h"
#include "fomod/FomodChoiceRecall.h"
#include "fomod/FomodEngine.h"
#include "install/ModInstaller.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace solero {

// Re-stage every mod in the active profile whose staging folder is missing or
// empty, from the archive in the downloads folder that matches its Nexus mod
// and file id. FOMODs replay the choices saved under fomod-choices/<id>.json.
// Headless: no dialogs, one line per mod on stdout.
int runRebuild(const QString& downloadsOverride) {
    QTextStream out(stdout);
    auto& cfg = AppConfig::instance();
    ProfileManager pm(AppConfig::dataRoot() + "/profiles");
    Profile* profile = pm.loadProfile(cfg.lastProfile());
    if (!profile) { out << "no active profile\n"; return 2; }
    const QString staging = cfg.stagingDir();
    const QString downloads = downloadsOverride.isEmpty() ? cfg.downloadsDir() : downloadsOverride;

    // (modId, fileId) -> archive, only where the archive itself still exists.
    QHash<QString, QString> archives;
    for (const QString& sc : QDir(downloads).entryList({"*.solero-nexus.json"}, QDir::Files)) {
        const QString archive = downloads + "/" + sc.chopped(int(strlen(".solero-nexus.json")));
        if (!QFile::exists(archive)) continue;
        QFile f(downloads + "/" + sc);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        archives.insert(o.value("modId").toString() + "/" + o.value("fileId").toString(), archive);
    }

    auto hasFiles = [](const QString& dir) {
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        return it.hasNext();
    };
    auto ciExists = [](const QString& dir, const QString& name) {
        for (const QString& e : QDir(dir).entryList(QDir::Files))
            if (e.compare(name, Qt::CaseInsensitive) == 0) return true;
        return false;
    };

    int done = 0, skipped = 0, missing = 0, failed = 0, nochoices = 0;
    for (const ModEntry& m : profile->modList().entries()) {
        if (m.type != EntryType::Mod) continue;
        const QString dir = stagingPathFor(staging, m);
        if (QDir(dir).exists() && hasFiles(dir)) { ++skipped; continue; }
        if (m.nexusModId.isEmpty() || m.nexusFileId.isEmpty()) {
            out << "NOID    " << m.name << "\n"; ++missing; continue;
        }
        const QString archive = archives.value(m.nexusModId + "/" + m.nexusFileId);
        if (archive.isEmpty()) { out << "NOARCH  " << m.name << " (" << m.nexusModId << "/" << m.nexusFileId << ")\n"; ++missing; continue; }
        // Guard against the empty-folder case that wiped the staging root once.
        if (QDir::cleanPath(dir) == QDir::cleanPath(staging)) { out << "BADDIR  " << m.name << "\n"; ++failed; continue; }
        out << "install " << m.name << " <- " << QFileInfo(archive).fileName() << "\n"; out.flush();

        InstallPrep prep = ModInstaller::prepare(archive);
        if (!prep.ok) { out << "  FAILED prepare: " << prep.errorMessage << "\n"; ++failed; continue; }
        InstallResult r;
        if (prep.layout.isFomod && !prep.fomodConfigPath.isEmpty()) {
            FomodEngine engine;
            if (!engine.load(prep.fomodConfigPath)) {
                r = ModInstaller::stageSimple(prep, staging, m.id, {}, dir);
            } else {
                engine.setFilePresent([&](const QString& file) {
                    if (ciExists(cfg.gameDir() + "/Data", file)) return true;
                    for (const ModEntry& o : profile->modList().entries())
                        if (o.type == EntryType::Mod && o.enabled
                            && ciExists(stagingPathFor(staging, o) + "/Data", file)) return true;
                    return false;
                });
                QFile cf(AppConfig::dataRoot() + "/fomod-choices/" + m.id + ".json");
                FomodPreset preset;
                if (cf.open(QIODevice::ReadOnly))
                    preset = buildFomodPreset(engine.module(), QJsonDocument::fromJson(cf.readAll()).object());
                if (preset.selection.isEmpty() && !engine.module().steps.isEmpty()) {
                    out << "  NOCHOICES (FOMOD with no saved picks; install by hand)\n"; ++nochoices; continue;
                }
                r = ModInstaller::stageFomod(prep, staging, engine.collectFiles(preset.selection), m.id, {}, dir);
            }
        } else {
            r = ModInstaller::stageSimple(prep, staging, m.id, {}, dir);
        }
        if (r.success) ++done; else { out << "  FAILED: " << r.errorMessage << "\n"; ++failed; }
        out.flush();
    }
    out << "rebuild: installed " << done << ", already present " << skipped << ", no archive/id " << missing
        << ", fomod without choices " << nochoices << ", failed " << failed << "\n";
    return failed ? 1 : 0;
}

} // namespace solero
