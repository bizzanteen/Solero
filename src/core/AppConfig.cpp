#include "AppConfig.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace solero {

AppConfig& AppConfig::instance() {
    static AppConfig s;
    return s;
}

QString AppConfig::configPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/config.json";
}

bool AppConfig::isConfigured() const {
    return !m_gameDir.isEmpty() && QDir(m_gameDir).exists();
}

void AppConfig::setGameDir(const QString& p) {
    m_gameDir = p;
    m_dataDir = p + "/Data";
}

void AppConfig::setStagingDir(const QString& p) {
    m_stagingDir = p;
}

bool AppConfig::load() {
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    auto obj = QJsonDocument::fromJson(f.readAll()).object();
    m_gameDir    = obj["gameDir"].toString();
    m_stagingDir = obj["stagingDir"].toString();
    m_dataDir    = m_gameDir.isEmpty() ? QString() : m_gameDir + "/Data";
    return true;
}

bool AppConfig::save() const {
    QDir().mkpath(QFileInfo(configPath()).path());
    QJsonObject obj;
    obj["gameDir"]    = m_gameDir;
    obj["stagingDir"] = m_stagingDir;
    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

QStringList AppConfig::detectSkyrimPaths() {
    QStringList candidates = {
        QDir::homePath() + "/.local/share/Steam/steamapps/common/Skyrim Special Edition",
        QDir::homePath() + "/.steam/steam/steamapps/common/Skyrim Special Edition",
        QDir::homePath() + "/.steam/root/steamapps/common/Skyrim Special Edition",
        QDir::homePath() + "/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/common/Skyrim Special Edition",
    };
    QStringList found;
    for (const auto& p : candidates) {
        if (QFile::exists(p + "/SkyrimSE.exe"))
            found.append(p);
    }
    return found;
}

} // namespace solero
