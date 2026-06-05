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

    // Persisted preferences
    bool confirmModDeletion() const          { return m_confirmModDeletion; }
    void setConfirmModDeletion(bool v)        { m_confirmModDeletion = v; }
    bool cycleSeparatorColors() const         { return m_cycleSeparatorColors; }
    void setCycleSeparatorColors(bool v)      { m_cycleSeparatorColors = v; }
    bool dataShowAllFiles() const             { return m_dataShowAllFiles; }
    void setDataShowAllFiles(bool v)          { m_dataShowAllFiles = v; }
    bool promptAfterBrowserDownload() const   { return m_promptAfterBrowserDownload; }
    void setPromptAfterBrowserDownload(bool v){ m_promptAfterBrowserDownload = v; }
    const QString& lastSeparatorColor() const { return m_lastSeparatorColor; }
    void setLastSeparatorColor(const QString& v) { m_lastSeparatorColor = v; }
    bool infoPanelVisible() const             { return m_infoPanelVisible; }
    void setInfoPanelVisible(bool v)          { m_infoPanelVisible = v; }
    bool autoCheckUpdates() const             { return m_autoCheckUpdates; }
    void setAutoCheckUpdates(bool v)          { m_autoCheckUpdates = v; }
    qint64 lastUpdateCheckEpoch() const       { return m_lastUpdateCheckEpoch; }
    void setLastUpdateCheckEpoch(qint64 v)    { m_lastUpdateCheckEpoch = v; }

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
    bool m_confirmModDeletion = true;
    bool m_cycleSeparatorColors = true;
    bool m_dataShowAllFiles = false;
    bool m_promptAfterBrowserDownload = true;
    bool m_infoPanelVisible = true;
    bool m_autoCheckUpdates = true;
    qint64 m_lastUpdateCheckEpoch = 0;
    QString m_lastSeparatorColor;
};

} // namespace solero
