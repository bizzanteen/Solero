#pragma once
#include <QString>

namespace solero {

class AppConfig {
public:
    static AppConfig& instance();

    bool isConfigured() const;
    bool load();
    bool save() const;

    const QString& gameDir() const     { return m_gameDir; }
    const QString& stagingDir() const  { return m_stagingDir; }
    const QString& dataDir() const     { return m_dataDir; }

    // Skyrim's runtime paths inside the Proton/Wine prefix.
    // localAppData holds Plugins.txt / loadorder.txt; documents holds the INIs.
    const QString& localAppDataDir() const { return m_localAppData; }
    const QString& documentsDir() const    { return m_documents; }
    const QString& downloadsDir() const    { return m_downloads; }
    void setDownloadsDir(const QString& p) { m_downloads = p; }
    QString toolsDir() const;

    void setGameDir(const QString& p);
    void setStagingDir(const QString& p);
    void setLocalAppDataDir(const QString& p) { m_localAppData = p; }
    void setDocumentsDir(const QString& p)    { m_documents = p; }

    static QString dataRoot();   // ~/.local/share/solero
    static QString configPath();
    static QStringList detectSkyrimPaths();

    // Derive the Proton-prefix local appdata + documents dirs from a game dir
    // (Steam appid 489830). Returns empty strings if not found.
    static QString detectLocalAppData(const QString& gameDir);
    static QString detectDocumentsDir(const QString& gameDir);

    // Find the Proton directory Skyrim uses (prefer GE). Empty if none found.
    QString detectProtonDir() const;

private:
    AppConfig() = default;
    QString m_gameDir;
    QString m_stagingDir;
    QString m_dataDir; // derived: gameDir/Data
    QString m_localAppData;
    QString m_documents;
    QString m_downloads;
};

} // namespace solero
