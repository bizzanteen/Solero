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

QString AppConfig::dataRoot() {
    return QDir::homePath() + "/.local/share/solero";
}

QString AppConfig::configPath() {
    return dataRoot() + "/config.json";
}

bool AppConfig::isConfigured() const {
    return !m_gameDir.isEmpty() && QDir(m_gameDir).exists();
}

void AppConfig::setGameDir(const QString& p) {
    m_gameDir = p;
    m_dataDir = p + "/Data";
    // Auto-detect the Proton-prefix runtime paths (only if not already set).
    if (m_localAppData.isEmpty()) m_localAppData = detectLocalAppData(p);
    if (m_documents.isEmpty())    m_documents    = detectDocumentsDir(p);
}

void AppConfig::setStagingDir(const QString& p) {
    m_stagingDir = p;
    if (m_downloads.isEmpty()) {
        QDir d(p); d.cdUp(); // parent of mods/
        m_downloads = d.absolutePath() + "/downloads";
    }
}

QString AppConfig::detectLocalAppData(const QString& gameDir) {
    // gameDir = <steamapps>/common/Skyrim Special Edition
    QDir d(gameDir);
    d.cdUp(); d.cdUp(); // -> steamapps
    QString candidate = d.absolutePath()
        + "/compatdata/489830/pfx/drive_c/users/steamuser/AppData/Local/Skyrim Special Edition";
    return QDir(candidate).exists() ? candidate : QString();
}

QString AppConfig::detectDocumentsDir(const QString& gameDir) {
    QDir d(gameDir);
    d.cdUp(); d.cdUp(); // -> steamapps
    QString candidate = d.absolutePath()
        + "/compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition";
    return QDir(candidate).exists() ? candidate : QString();
}

bool AppConfig::load() {
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    auto obj = QJsonDocument::fromJson(f.readAll()).object();
    m_gameDir      = obj["gameDir"].toString();
    m_stagingDir   = obj["stagingDir"].toString();
    m_localAppData = obj["localAppDataDir"].toString();
    m_documents    = obj["documentsDir"].toString();
    m_downloads    = obj["downloadsDir"].toString();
    if (m_downloads.isEmpty() && !m_stagingDir.isEmpty()) {
        QDir d(m_stagingDir); d.cdUp();
        m_downloads = d.absolutePath() + "/downloads";
    }
    m_dataDir      = m_gameDir.isEmpty() ? QString() : m_gameDir + "/Data";
    // Back-fill runtime paths for configs written before this field existed.
    if (m_localAppData.isEmpty() && !m_gameDir.isEmpty())
        m_localAppData = detectLocalAppData(m_gameDir);
    if (m_documents.isEmpty() && !m_gameDir.isEmpty())
        m_documents = detectDocumentsDir(m_gameDir);
    return true;
}

bool AppConfig::save() const {
    QDir().mkpath(QFileInfo(configPath()).path());
    QJsonObject obj;
    obj["gameDir"]         = m_gameDir;
    obj["stagingDir"]      = m_stagingDir;
    obj["localAppDataDir"] = m_localAppData;
    obj["documentsDir"]    = m_documents;
    obj["downloadsDir"]    = m_downloads;
    QFile f(configPath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

QString AppConfig::detectProtonDir() const {
    QStringList bases = {
        QDir::homePath() + "/.local/share/Steam/compatibilitytools.d",
        QDir::homePath() + "/.steam/root/compatibilitytools.d",
    };
    if (!m_gameDir.isEmpty()) { QDir g(m_gameDir); g.cdUp(); /* common */ bases << g.absolutePath(); }
    QString first;
    for (const QString& base : bases) {
        QDir d(base);
        if (!d.exists()) continue;
        for (const QString& sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QString dir = base + "/" + sub;
            if (!QFile::exists(dir + "/proton")) continue;
            if (first.isEmpty()) first = dir;
            if (sub.contains("GE", Qt::CaseInsensitive)) return dir;
        }
    }
    return first;
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
