#include "RadiumPrep.h"
#include "core/Profile.h"
#include "core/PluginList.h"
#include "core/ModList.h"
#include "core/Types.h"
#include "core/FileUtil.h"
#include "core/StagingFolder.h"
#include "core/AppConfig.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace solero {
namespace RadiumPrep {

QString defaultSettingsPath() {
    return QDir::homePath() + "/.config/radium-textures/settings.json";
}

QString mo2ModFolder(const ModEntry& e) {
    if (e.type == EntryType::Separator)
        return sanitizeStagingFolder(e.name) + "_separator";
    // Mod: prefer the staging folder; fall back to the id (older saves) so the
    // mods/<folder> entry and the modlist line resolve to the same on-disk dir.
    return e.stagingFolder.isEmpty() ? e.id : e.stagingFolder;
}

QStringList buildMo2Modlist(const ModList& modList) {
    // MO2 modlist.txt top line = highest priority; Solero modList() index 0 =
    // lowest priority. So emit lines from last index down to first (reversed).
    QStringList lines;
    for (int i = modList.count() - 1; i >= 0; --i) {
        const ModEntry& e = modList.at(i);
        const QString folder = mo2ModFolder(e);
        if (e.type == EntryType::Separator)
            lines << ("-" + folder);                 // separators are inert (disabled)
        else
            lines << ((e.enabled ? "+" : "-") + folder);
    }
    return lines;
}

// Populate <fakeMo2Dir>/mods + profiles/solero/modlist.txt from the active
// profile so MO2 mode can detect inter-mod conflicts. PGPatcher-only.
static bool populateMo2Instance(const Profile& profile,
                                const QString& stagingDir,
                                const QString& modsDir,
                                const QString& profDir,
                                QString* error) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };

    // 1. Stale cleanup of mods/ (the instance regenerates each run). We only ever
    //    create symlinks + empty dirs here, so removing them is safe and never
    //    touches staged data. never follow a symlink: remove the LINK itself.
    const QFileInfoList stale =
        QDir(modsDir).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries |
                                    QDir::System | QDir::Hidden);
    for (const QFileInfo& fi : stale) {
        if (fi.isSymLink()) {
            QFile::remove(fi.absoluteFilePath());      // removes the link, not target
        } else if (fi.isDir()) {
            QDir(fi.absoluteFilePath()).removeRecursively();
        } else {
            QFile::remove(fi.absoluteFilePath());
        }
    }

    // 2. Build modlist (top = highest priority = reversed mod-list order) and the
    //    matching mods/ entries. The folder token must match exactly or PGPatcher
    //    aborts with "modlist.txt does not reflect the contents of the mods folder".
    const ModList& ml = profile.modList();
    const QStringList lines = buildMo2Modlist(ml);

    for (int i = 0; i < ml.count(); ++i) {
        const ModEntry& e = ml.at(i);
        const QString folder = mo2ModFolder(e);
        const QString dst = modsDir + "/" + folder;
        if (e.type == EntryType::Separator) {
            if (!QDir().mkpath(dst)) return fail("Could not create " + dst);
            continue;
        }
        // Mod: the staged content lives under <stagingDir>/<folder>/Data/; MO2
        // wants meshes/textures at the mod root, so symlink the Data subdir.
        const QString src = stagingPathFor(stagingDir, e) + "/Data";
        if (QFileInfo::exists(src)) {
            QFile::remove(dst); // defensive: clear any leftover before relinking
            if (!QFile::link(src, dst)) return fail("Could not link " + dst);
        } else {
            // game-root-only mod (no Data) - empty dir so the modlist line still
            // has a matching folder (integrity invariant).
            if (!QDir().mkpath(dst)) return fail("Could not create " + dst);
        }
    }

    if (!atomicWrite(profDir + "/modlist.txt",
                     lines.join("\n").toUtf8() + (lines.isEmpty() ? "" : "\n")))
        return fail("Could not write modlist.txt");

    // 5. plugins.txt - PGPatcher/PGLib references it. MO2 marks active plugins with
    //    a leading '*'. Mirror loadorder.txt's enabled-plugin set/order.
    QStringList plugins;
    const PluginList& pl = profile.pluginList();
    for (int i = 0; i < pl.count(); ++i)
        if (pl.at(i).enabled) plugins << ("*" + pl.at(i).filename);
    if (!atomicWrite(profDir + "/plugins.txt",
                     plugins.join("\n").toUtf8() + (plugins.isEmpty() ? "" : "\n")))
        return fail("Could not write plugins.txt");

    return true;
}

bool writeFakeMo2(const Profile& profile,
                  const QString& gameDir,
                  const QString& fakeMo2Dir,
                  QString* error,
                  bool populateMods) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };

    const QString fakeMo2 = fakeMo2Dir;
    const QString modsDir  = fakeMo2 + "/mods";
    const QString profDir  = fakeMo2 + "/profiles/solero";
    const QString dataDir  = gameDir + "/Data";

    if (!QDir().mkpath(modsDir)) return fail("Could not create " + modsDir);
    if (!QDir().mkpath(profDir)) return fail("Could not create " + profDir);

    QStringList order;
    const PluginList& pl = profile.pluginList();
    for (int i = 0; i < pl.count(); ++i) {
        const PluginEntry& e = pl.at(i);
        if (e.enabled) order << e.filename;
    }
    const QByteArray loadorder =
        (order.join('\n') + (order.isEmpty() ? "" : "\n")).toUtf8();
    if (!atomicWrite(profDir + "/loadorder.txt", loadorder))
        return fail("Could not write loadorder.txt");

    QStringList bsas = QDir(dataDir).entryList(QStringList{"*.bsa"},
                                               QDir::Files, QDir::Name);
    const QByteArray archives =
        (bsas.join('\n') + (bsas.isEmpty() ? "" : "\n")).toUtf8();
    if (!atomicWrite(profDir + "/archives.txt", archives))
        return fail("Could not write archives.txt");

    if (populateMods) {
        // PGPatcher path: populate a real MO2 instance (mods/ symlinks +
        // modlist.txt + plugins.txt) so MO2 mode detects inter-mod conflicts.
        if (!populateMo2Instance(profile, AppConfig::instance().stagingDir(),
                                 modsDir, profDir, error))
            return false;
    } else if (!atomicWrite(profDir + "/modlist.txt", QByteArray())) {
        // Radium path: empty mods/ + empty modlist.txt (unchanged behavior).
        return fail("Could not write modlist.txt");
    }

    // ModOrganizer.ini: the GUI's MO2 Setup panel requires this file at the
    // mo2_folder root to detect the game and enumerate profiles - manual_mode
    // does not bypass it (the CLI path does, but we drive the GUI). Radium reads
    // gameName + selected_profile; gamePath is a Wine-style path where it maps a
    // doubled backslash back to a forward slash, so each '/' becomes "\\".
    QString winePath = gameDir;
    winePath.replace('/', QStringLiteral("\\\\"));
    const QString ini =
        "[General]\n"
        "gameName=Skyrim Special Edition\n"
        "gamePath=@ByteArray(Z:" + winePath + ")\n"
        "game_edition=Steam\n"
        "selected_profile=@ByteArray(solero)\n"
        "version=2.5.2\n"
        "first_start=false\n";
    if (!atomicWrite(fakeMo2 + "/ModOrganizer.ini", ini.toUtf8()))
        return fail("Could not write ModOrganizer.ini");

    return true;
}

bool prepare(const Profile& profile,
             const QString& gameDir,
             const QString& installDir,
             const QString& outputDataDir,
             const QString& settingsPath,
             QString* error) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };

    const QString fakeMo2 = installDir + "/fake-mo2";
    if (!writeFakeMo2(profile, gameDir, fakeMo2, error))
        return false;

    const QString modsDir = fakeMo2 + "/mods";
    const QString profDir = fakeMo2 + "/profiles/solero";
    const QString dataDir = gameDir + "/Data";

    QJsonObject obj;
    {
        QFile f(settingsPath);
        if (f.open(QIODevice::ReadOnly))
            obj = QJsonDocument::fromJson(f.readAll()).object();
    }
    obj["manual_mode"] = true;
    obj["game"]          = "SkyrimSE";
    obj["mo2_folder"]    = fakeMo2;
    obj["selected_profile"] = "solero";
    obj["profile_path"]  = profDir;
    obj["mods_path"]     = modsDir;
    obj["data_path"]     = dataDir;
    obj["output_path"]   = outputDataDir;

    if (!QDir().mkpath(QFileInfo(settingsPath).path()))
        return fail("Could not create config dir for " + settingsPath);
    if (!atomicWrite(settingsPath, QJsonDocument(obj).toJson(QJsonDocument::Indented)))
        return fail("Could not write " + settingsPath);

    return true;
}

} // namespace RadiumPrep
} // namespace solero
