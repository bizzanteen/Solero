#include "AppConfig.h"
#include "FileUtil.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QRegularExpression>

namespace solero {

AppConfig& AppConfig::instance() {
    static AppConfig s;
    return s;
}

QString AppConfig::dataRoot() {
    return QDir::homePath() + "/.local/share/solero";
}

QString AppConfig::overwriteDir(const QString& profileName) {
    const QString base = dataRoot() + "/overwrite";
    if (profileName.isEmpty()) return base; // legacy/global fallback
    // Sanitize for use as a single path component.
    QString safe = profileName;
    safe.replace('/', '_').replace('\\', '_');
    return base + "/" + safe;
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
    m_autoDeployBeforeLaunch = obj["autoDeployBeforeLaunch"].toBool(false);
    m_infoPanelVisible     = obj["infoPanelVisible"].toBool(true);
    m_autoCheckUpdates     = obj["autoCheckUpdates"].toBool(true);
    m_lastUpdateCheckEpoch = static_cast<qint64>(obj["lastUpdateCheckEpoch"].toDouble(0));
    m_lastSeparatorColor   = obj["lastSeparatorColor"].toString();
    m_jackifyEnginePath    = obj["jackifyEnginePath"].toString();
    const QString dm = obj["deployMode"].toString("hardlink");
    m_deployMode = (dm == "symlink") ? DeployMode::SymLink
                 : (dm == "copy")    ? DeployMode::Copy
                                     : DeployMode::HardLink;
    m_lastProfile = obj["lastProfile"].toString();
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
    obj["autoDeployBeforeLaunch"] = m_autoDeployBeforeLaunch;
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

QStringList AppConfig::parseLibraryFoldersVdf(const QString& vdfContents) {
    // Match every  "path"  "<value>"  pair (VDF uses tab/space separators). Both
    // the legacy flat and modern nested forms expose library roots under "path".
    static const QRegularExpression re(
        QStringLiteral("\"path\"\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    QStringList out;
    auto it = re.globalMatch(vdfContents);
    while (it.hasNext()) {
        QString p = it.next().captured(1);
        p.replace("\\\\", "\\"); // VDF escapes backslashes (Windows paths)
        if (!p.isEmpty() && !out.contains(p)) out.append(p);
    }
    return out;
}

QStringList AppConfig::detectSkyrimPaths() {
    // Steam roots to probe for libraryfolders.vdf.
    const QStringList steamRoots = {
        QDir::homePath() + "/.local/share/Steam",
        QDir::homePath() + "/.steam/steam",
        QDir::homePath() + "/.steam/root",
        QDir::homePath() + "/.var/app/com.valvesoftware.Steam/data/Steam",
    };

    // Enumerate every Steam library from libraryfolders.vdf across all Steam roots,
    // then probe each for the game. This catches games installed on secondary
    // drives/libraries that the fixed-path list below would miss.
    QStringList libraryRoots;
    for (const QString& root : steamRoots) {
        const QString vdf = root + "/steamapps/libraryfolders.vdf";
        QFile f(vdf);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString contents = QString::fromUtf8(f.readAll());
        for (const QString& lib : parseLibraryFoldersVdf(contents))
            if (!libraryRoots.contains(lib)) libraryRoots.append(lib);
        // The Steam root itself is always an implicit library.
        if (!libraryRoots.contains(root)) libraryRoots.append(root);
    }

    QStringList found;
    auto probe = [&](const QString& gameDir) {
        if (QFile::exists(gameDir + "/SkyrimSE.exe") && !found.contains(gameDir))
            found.append(gameDir);
    };
    for (const QString& lib : libraryRoots)
        probe(lib + "/steamapps/common/Skyrim Special Edition");

    // Fixed fallbacks (in case no vdf was readable) - harmless dedupe via probe().
    const QStringList fixed = {
        QDir::homePath() + "/.local/share/Steam/steamapps/common/Skyrim Special Edition",
        QDir::homePath() + "/.steam/steam/steamapps/common/Skyrim Special Edition",
        QDir::homePath() + "/.steam/root/steamapps/common/Skyrim Special Edition",
        QDir::homePath() + "/.var/app/com.valvesoftware.Steam/data/Steam/steamapps/common/Skyrim Special Edition",
    };
    for (const QString& p : fixed) probe(p);
    return found;
}

} // namespace solero
