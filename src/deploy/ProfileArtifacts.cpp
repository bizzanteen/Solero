#include "ProfileArtifacts.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include "core/FileUtil.h"
#include "core/Log.h"
#include "core/StagingFolder.h"
#include "bethini/IniFile.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QCryptographicHash>

namespace solero {

// Plugins.txt / loadorder.txt belong in the game's local appdata folder (inside the
// Proton prefix), where Skyrim actually reads them. Fall back to Data/ when the
// prefix isn't known - the game won't read it there, but MainWindow warns about that
// separately and a discoverable file beats a silently discarded one.
QString loadOrderDir(const QString& gameDir) {
    const auto& cfg = AppConfig::instance();
    if (cfg.gameType() == AppConfig::oblivionRemasteredGame())
        return cfg.dataDir();
    const QString localAppData = AppConfig::instance().localAppDataDir();
    return localAppData.isEmpty() ? (gameDir + "/Data") : localAppData;
}

// INIs belong in the game's My Games documents folder. Fall back to the game dir.
QString iniDir(const QString& gameDir) {
    const QString docs = AppConfig::instance().documentsDir();
    return docs.isEmpty() ? gameDir : docs;
}

static bool writeUe4ssModList(Profile& profile, const QString& gameDir,
                              QString* pathOut) {
    QSet<QString> managed;
    QStringList enabled;
    const QString staging = AppConfig::instance().stagingDir();
    for (const ModEntry& mod : profile.modList()) {
        if (mod.type != EntryType::Mod) continue;
        const QString root = stagingPathFor(staging, mod);
        QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            QString rel = QDir(root).relativeFilePath(it.next());
            rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
            const QString lower = rel.toLower();
            const int marker = lower.indexOf(QStringLiteral("ue4ss/mods/"));
            if (marker < 0) continue;
            const QString tail = rel.mid(marker + 11);
            const int slash = tail.indexOf('/');
            if (slash <= 0) continue;
            const QString name = tail.left(slash);
            managed.insert(name);
            if (mod.enabled && !enabled.contains(name, Qt::CaseInsensitive))
                enabled.append(name);
        }
    }
    const QString modsDir = gameDir + "/OblivionRemastered/Binaries/Win64/ue4ss/Mods";
    const QString modsTxt = modsDir + "/mods.txt";

    const QString stateDir = AppConfig::dataRoot() + "/ue4ss";
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(QFile::encodeName(QDir::cleanPath(gameDir)),
                                 QCryptographicHash::Sha256).toHex());
    const QString statePath = stateDir + "/" + key + ".txt";
    QSet<QString> previouslyManaged;
    QFile stateIn(statePath);
    if (stateIn.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!stateIn.atEnd()) {
            const QString name = QString::fromUtf8(stateIn.readLine()).trimmed();
            if (!name.isEmpty()) previouslyManaged.insert(name);
        }
    }
    if (managed.isEmpty() && previouslyManaged.isEmpty()) return true;
    QDir().mkpath(modsDir);

    QStringList lines;
    QFile live(modsTxt);
    if (live.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!live.atEnd()) {
            const QString line = QString::fromUtf8(live.readLine()).trimmed();
            if (line.isEmpty()) continue;
            const int colon = line.lastIndexOf(':');
            const QString name = (colon < 0 ? line : line.left(colon)).trimmed();
            bool managedName = false;
            for (const QString& known : previouslyManaged)
                if (name.compare(known, Qt::CaseInsensitive) == 0) { managedName = true; break; }
            for (const QString& known : managed)
                if (name.compare(known, Qt::CaseInsensitive) == 0) { managedName = true; break; }
            if (!managedName) lines.append(line);
        }
    }
    for (const QString& name : enabled) lines.append(name + " : 1");

    QSaveFile output(modsTxt);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const QByteArray contents = (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    if (output.write(contents) != contents.size() || !output.commit()) return false;
    QSaveFile stateOut(statePath);
    if (stateOut.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QStringList managedNames;
        for (const QString& name : managed) managedNames.append(name);
        const QByteArray state = (managedNames.join(QLatin1Char('\n'))
                                  + QLatin1Char('\n')).toUtf8();
        stateOut.write(state);
        stateOut.commit();
    }
    *pathOut = modsTxt;
    return true;
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
    if (AppConfig::instance().gameType() == AppConfig::oblivionRemasteredGame()) {
        QString modsTxt;
        if (!writeUe4ssModList(profile, gameDir, &modsTxt))
            wrote(gameDir + "/OblivionRemastered/Binaries/Win64/ue4ss/Mods/mods.txt",
                  false, "the UE4SS mod list");
        else if (!modsTxt.isEmpty())
            result.written.append(modsTxt);
        return result;
    }
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
