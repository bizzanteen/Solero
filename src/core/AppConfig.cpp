#include "AppConfig.h"
#include "FileUtil.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
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
    m_confirmModDeletion   = obj["confirmModDeletion"].toBool(true);
    m_shaderCacheDeclined  = obj["shaderCacheDeclined"].toBool(false);
    m_cycleSeparatorColors = obj["cycleSeparatorColors"].toBool(true);
    m_dataShowAllFiles     = obj["dataShowAllFiles"].toBool(false);
    m_promptAfterBrowserDownload = obj["promptAfterBrowserDownload"].toBool(true);
    m_infoPanelVisible     = obj["infoPanelVisible"].toBool(true);
    m_autoCheckUpdates     = obj["autoCheckUpdates"].toBool(true);
    m_lastUpdateCheckEpoch = static_cast<qint64>(obj["lastUpdateCheckEpoch"].toDouble(0));
    m_lastSeparatorColor   = obj["lastSeparatorColor"].toString();
    m_jackifyEnginePath    = obj["jackifyEnginePath"].toString();
    const QString dm = obj["deployMode"].toString("hardlink");
    m_deployMode = (dm == "symlink") ? DeployMode::SymLink
                 : (dm == "copy")    ? DeployMode::Copy
                                     : DeployMode::HardLink;
    m_hiddenColumns.clear();
    for (const auto& v : obj["hiddenColumns"].toArray())
        m_hiddenColumns.append(v.toInt());
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
    obj["confirmModDeletion"]   = m_confirmModDeletion;
    obj["shaderCacheDeclined"]  = m_shaderCacheDeclined;
    obj["cycleSeparatorColors"] = m_cycleSeparatorColors;
    obj["dataShowAllFiles"]     = m_dataShowAllFiles;
    obj["promptAfterBrowserDownload"] = m_promptAfterBrowserDownload;
    obj["infoPanelVisible"]     = m_infoPanelVisible;
    obj["autoCheckUpdates"]     = m_autoCheckUpdates;
    obj["lastUpdateCheckEpoch"] = static_cast<double>(m_lastUpdateCheckEpoch);
    obj["lastSeparatorColor"]   = m_lastSeparatorColor;
    obj["jackifyEnginePath"]    = m_jackifyEnginePath;
    obj["deployMode"] = (m_deployMode == DeployMode::SymLink) ? "symlink"
                      : (m_deployMode == DeployMode::Copy)    ? "copy"
                                                              : "hardlink";
    QJsonArray hidden;
    for (int c : m_hiddenColumns) hidden.append(c);
    obj["hiddenColumns"] = hidden;
    return atomicWrite(configPath(), QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

QString AppConfig::toolsDir() const {
    if (m_stagingDir.isEmpty())
        return QDir::homePath() + "/Modding/Solero/tools";
    QDir d(m_stagingDir); d.cdUp();
    return d.absolutePath() + "/tools";
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
