#include "RadiumPrep.h"
#include "core/Profile.h"
#include "core/PluginList.h"
#include "core/Types.h"
#include "core/FileUtil.h"
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

bool prepare(const Profile& profile,
             const QString& gameDir,
             const QString& installDir,
             const QString& outputDataDir,
             const QString& settingsPath,
             QString* error) {
    auto fail = [&](const QString& m) { if (error) *error = m; return false; };

    const QString fakeMo2 = installDir + "/fake-mo2";
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

    if (!atomicWrite(profDir + "/modlist.txt", QByteArray()))
        return fail("Could not write modlist.txt");

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
