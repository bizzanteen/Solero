#include "Profile.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace solero {

Profile::Profile(const QString& name, const QString& rootPath)
    : m_name(name), m_path(rootPath + "/" + name) {}

QString Profile::modlistPath()       const { return m_path + "/modlist.json"; }
QString Profile::pluginsPath()       const { return m_path + "/plugins.txt"; }
QString Profile::skyrimIniPath()     const { return m_path + "/Skyrim.ini"; }
QString Profile::skyrimPrefsPath()   const { return m_path + "/SkyrimPrefs.ini"; }
QString Profile::skyrimCustomPath()  const { return m_path + "/SkyrimCustom.ini"; }
QString Profile::executablesPath()   const { return m_path + "/executables.json"; }
QString Profile::lootUserlistPath()  const { return m_path + "/loot-userlist.yaml"; }

bool Profile::save() const {
    QDir().mkpath(m_path);
    if (!m_modList.saveToFile(modlistPath())) return false;
    if (!m_pluginList.saveToFile(pluginsPath())) return false;
    return saveExecutables();
}

bool Profile::load() {
    if (QFile::exists(modlistPath()))
        m_modList = ModList::loadFromFile(modlistPath());
    if (QFile::exists(pluginsPath()))
        m_pluginList = PluginList::loadFromFile(pluginsPath());
    loadExecutables();
    return true;
}

bool Profile::saveExecutables() const {
    QJsonArray arr;
    for (const auto& e : m_executables) {
        QJsonObject o;
        o["id"]                = e.id;
        o["name"]              = e.name;
        o["binaryPath"]        = e.binaryPath;
        o["workingDir"]        = e.workingDir;
        o["arguments"]         = e.arguments;
        o["runtime"]           = (e.runtime == RuntimeType::Proton) ? "proton" : "native";
        o["protonVersion"]     = e.protonVersion;
        o["winePrefix"]        = e.winePrefix;
        o["runThroughDeployer"]= e.runThroughDeployer;
        o["isPrimary"]         = e.isPrimary;
        o["isCapturingOutput"] = e.isCapturingOutput;
        o["outputModId"]       = e.outputModId;
        arr.append(o);
    }
    QFile f(executablesPath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

bool Profile::loadExecutables() {
    QFile f(executablesPath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
        auto o = v.toObject();
        Executable e;
        e.id                = o["id"].toString();
        e.name              = o["name"].toString();
        e.binaryPath        = o["binaryPath"].toString();
        e.workingDir        = o["workingDir"].toString();
        e.arguments         = o["arguments"].toString();
        e.runtime           = (o["runtime"].toString() == "proton") ? RuntimeType::Proton : RuntimeType::Native;
        e.protonVersion     = o["protonVersion"].toString();
        e.winePrefix        = o["winePrefix"].toString();
        e.runThroughDeployer= o["runThroughDeployer"].toBool(true);
        e.isPrimary         = o["isPrimary"].toBool(false);
        e.isCapturingOutput = o["isCapturingOutput"].toBool(false);
        e.outputModId       = o["outputModId"].toString();
        m_executables.append(e);
    }
    return true;
}

} // namespace solero
