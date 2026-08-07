#include "ProfileArtifacts.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include "core/FileUtil.h"
#include "core/Log.h"
#include "bethini/IniFile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace solero {

// Plugins.txt / loadorder.txt belong in the game's local appdata folder (inside the
// Proton prefix), where Skyrim actually reads them. Fall back to Data/ when the
// prefix isn't known - the game won't read it there, but MainWindow warns about that
// separately and a discoverable file beats a silently discarded one.
QString loadOrderDir(const QString& gameDir) {
    const QString localAppData = AppConfig::instance().localAppDataDir();
    return localAppData.isEmpty() ? (gameDir + "/Data") : localAppData;
}

// INIs belong in the game's My Games documents folder. Fall back to the game dir.
QString iniDir(const QString& gameDir) {
    const QString docs = AppConfig::instance().documentsDir();
    return docs.isEmpty() ? gameDir : docs;
}

ArtifactWriteResult writeProfileArtifacts(Profile& profile, const QString& gameDir) {
    ArtifactWriteResult result;
    auto wrote = [&](const QString& path, bool ok, const QString& what) {
        if (ok) {
            result.written.append(path);
        } else {
            result.ok = false;
            result.errors.append(QString("Could not write %1 (%2).").arg(what, path));
            qCWarning(lcDeploy) << "artifact write failed:" << path;
        }
    };

    const QString orderDir = loadOrderDir(gameDir);
    QDir().mkpath(orderDir);
    // An empty plugin list would write an empty Plugins.txt and the game would
    // load nothing, so a profile whose plugins aren't scanned yet must not clobber
    // a live order. Keyed on parsed entries, not file size.
    if (profile.pluginList().count() == 0 &&
        PluginList::loadFromFile(orderDir + "/Plugins.txt").count() > 0) {
        qCWarning(lcDeploy) << "skipping load-order write: profile has no plugins but"
                            << orderDir + "/Plugins.txt" << "lists some";
        result.inPlace.append(orderDir + "/Plugins.txt");
        if (QFile::exists(orderDir + "/loadorder.txt"))
            result.inPlace.append(orderDir + "/loadorder.txt");
    } else {
        wrote(orderDir + "/Plugins.txt",
              profile.pluginList().saveToFile(orderDir + "/Plugins.txt"), "the load order");
        // loadorder.txt records the full order (all plugins, no prefix) so the game
        // and tools agree on the master/light/esp ordering, not just what's active.
        wrote(orderDir + "/loadorder.txt",
              profile.pluginList().saveLoadOrderToFile(orderDir + "/loadorder.txt"),
              "the full load order");
    }

    const QString inis = iniDir(gameDir);
    QDir().mkpath(inis);

    // INIs: only when the profile owns its INIs (localInis), and only the ones it
    // actually has. When off, the live/shared My Games INIs are left untouched.
    if (profile.localInis()) {
        const QStringList src = {
            profile.skyrimIniPath(),
            profile.skyrimPrefsPath(),
            profile.skyrimCustomPath()
        };
        const QStringList dst = {
            inis + "/Skyrim.ini",
            inis + "/SkyrimPrefs.ini",
            inis + "/SkyrimCustom.ini"
        };
        for (int i = 0; i < src.size(); ++i)
            if (QFile::exists(src[i]))
                wrote(dst[i], copyOverwrite(src[i], dst[i]), "an INI");
    }

    // On: point SLocalSavePath at a per-profile "Saves\<name>\", patching the live
    // Skyrim.ini. Off: strip the one we left, so saves go back to the shared
    // folder. The per-profile folder stays on disk either way.
    const QString saveIniPath = inis + "/Skyrim.ini";
    if (profile.localSaves()) {
        const QString sub = profile.saveFolderName();
        IniFile ini;
        ini.load(saveIniPath); // tolerant when the file doesn't exist yet
        ini.setValue("General", "SLocalSavePath", "Saves\\" + sub + "\\");
        const bool ok = ini.save(saveIniPath);
        if (ok) QDir().mkpath(inis + "/Saves/" + sub);
        wrote(saveIniPath, ok, "the save-folder redirect");
    } else if (QFile::exists(saveIniPath)) {
        IniFile ini;
        ini.load(saveIniPath);
        if (ini.has("General", "SLocalSavePath")) {
            ini.remove("General", "SLocalSavePath");
            wrote(saveIniPath, ini.save(saveIniPath), "the save-folder redirect");
        }
    }

    return result;
}

QString liveLoadOrderMismatch(const Profile& profile, const QString& gameDir) {
    const QString livePath = loadOrderDir(gameDir) + "/Plugins.txt";
    QFile f(livePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString("The game's Plugins.txt (%1) could not be read.").arg(livePath);
    const PluginList live = PluginList::fromPluginsTxt(QString::fromUtf8(f.readAll()));

    // Only the enabled set and its order reach the game; disabled plugins may or may
    // not be listed, so comparing them would report differences that don't matter.
    auto enabledNames = [](const PluginList& list) {
        QStringList names;
        for (int i = 0; i < list.count(); ++i)
            if (list.at(i).enabled) names.append(list.at(i).filename);
        return names;
    };
    const QStringList want = enabledNames(profile.pluginList());
    const QStringList have = enabledNames(live);

    for (int i = 0; i < qMin(want.size(), have.size()); ++i) {
        if (want[i].compare(have[i], Qt::CaseInsensitive) == 0) continue;
        return QString("%1 should be at position %2 in the load order, but the game "
                       "has %3 there.").arg(want[i]).arg(i + 1).arg(have[i]);
    }
    if (want.size() != have.size())
        return QString("The profile has %1 enabled plugin(s) but the game's "
                       "Plugins.txt lists %2.").arg(want.size()).arg(have.size());
    return {};
}

} // namespace solero
