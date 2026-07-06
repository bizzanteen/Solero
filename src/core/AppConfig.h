#pragma once
#include <QString>
#include <QByteArray>
#include <QList>
#include "deploy/DeployMode.h"

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
    // Skyrim's Saves directory, derived read-only from documentsDir (the "Saves"
    // subfolder of the game's My Games documents dir). Empty when the documents
    // dir is unknown. Solero only ever READS this - it never touches save files.
    QString savesDir() const {
        return m_documents.isEmpty() ? QString() : m_documents + "/Saves";
    }
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
    // One-time "let Solero manage the Community Shaders cache?" offer was declined.
    bool shaderCacheDeclined() const          { return m_shaderCacheDeclined; }
    void setShaderCacheDeclined(bool v)       { m_shaderCacheDeclined = v; }
    // One-time migration of the legacy global tool template into the active
    // profile's per-profile executables. Set true after the migration runs once.
    bool toolsMigratedToPerProfile() const    { return m_toolsMigratedToPerProfile; }
    void setToolsMigratedToPerProfile(bool v) { m_toolsMigratedToPerProfile = v; }
    // One-time migration that renames existing output-mod staging folders to be
    // profile-qualified ("<Profile> - <name>") so two profiles no longer share a
    // single bare folder. Set true after the migration runs once.
    bool outputModsProfileQualified() const   { return m_outputModsProfileQualified; }
    void setOutputModsProfileQualified(bool v){ m_outputModsProfileQualified = v; }
    bool cycleSeparatorColors() const         { return m_cycleSeparatorColors; }
    void setCycleSeparatorColors(bool v)      { m_cycleSeparatorColors = v; }
    bool dataShowAllFiles() const             { return m_dataShowAllFiles; }
    void setDataShowAllFiles(bool v)          { m_dataShowAllFiles = v; }
    bool promptAfterBrowserDownload() const   { return m_promptAfterBrowserDownload; }
    void setPromptAfterBrowserDownload(bool v){ m_promptAfterBrowserDownload = v; }
    // When true, deploy silently before launching the game/a tool instead of
    // popping the "deploy required" modal. Default false (always prompt).
    bool autoDeployBeforeLaunch() const       { return m_autoDeployBeforeLaunch; }
    void setAutoDeployBeforeLaunch(bool v)    { m_autoDeployBeforeLaunch = v; }
    const QString& lastSeparatorColor() const { return m_lastSeparatorColor; }
    void setLastSeparatorColor(const QString& v) { m_lastSeparatorColor = v; }
    bool infoPanelVisible() const             { return m_infoPanelVisible; }
    void setInfoPanelVisible(bool v)          { m_infoPanelVisible = v; }
    bool autoCheckUpdates() const             { return m_autoCheckUpdates; }
    void setAutoCheckUpdates(bool v)          { m_autoCheckUpdates = v; }
    // One-shot "enable detailed logging on the next launch only" flag, set by the
    // crash-report dialog. main() reads it after installLogging(), turns verbose
    // logging on for this run, then clears + saves it (so it lasts exactly one run).
    bool verboseNextLaunch() const            { return m_verboseNextLaunch; }
    void setVerboseNextLaunch(bool v)         { m_verboseNextLaunch = v; }
    qint64 lastUpdateCheckEpoch() const       { return m_lastUpdateCheckEpoch; }
    void setLastUpdateCheckEpoch(qint64 v)    { m_lastUpdateCheckEpoch = v; }
    QString jackifyEnginePath() const         { return m_jackifyEnginePath; }
    void setJackifyEnginePath(const QString& v) { m_jackifyEnginePath = v; }
    DeployMode deployMode() const             { return m_deployMode; }
    void setDeployMode(DeployMode v)          { m_deployMode = v; }
    // UI theme options (applied by ThemeAdapter). themeMode: "system" (follow KDE,
    // default) | "light" | "dark". accentColor: "#rrggbb" or empty (palette default).
    // fontFamily empty / fontSize 0 = Qt default.
    QString themeMode() const                 { return m_themeMode; }
    void setThemeMode(const QString& v)       { m_themeMode = v; }
    QString accentColor() const               { return m_accentColor; }
    void setAccentColor(const QString& v)     { m_accentColor = v; }
    QString fontFamily() const                { return m_fontFamily; }
    void setFontFamily(const QString& v)      { m_fontFamily = v; }
    int fontSize() const                      { return m_fontSize; }
    void setFontSize(int v)                   { m_fontSize = v; }
    // Name of the profile active when Solero last closed; restored on next launch.
    const QString& lastProfile() const        { return m_lastProfile; }
    void setLastProfile(const QString& v)     { m_lastProfile = v; }
    // Preferred Nexus CDN mirror short_name (empty = automatic / first mirror).
    const QString& preferredDownloadServer() const  { return m_preferredDownloadServer; }
    void setPreferredDownloadServer(const QString& v) { m_preferredDownloadServer = v; }
    // Most recent set of mirror short_names seen from download_link.json, for the
    // Settings combo. Updated in-memory whenever a download URL is resolved.
    const QStringList& cachedDownloadServers() const { return m_cachedDownloadServers; }
    void setCachedDownloadServers(const QStringList& v) { m_cachedDownloadServers = v; }
    // Hidden mod-list columns (ModListModel::Column indices). Name is never hidden.
    const QList<int>& hiddenColumns() const   { return m_hiddenColumns; }
    void setHiddenColumns(const QList<int>& v) { m_hiddenColumns = v; }
    // Persisted QHeaderView::saveState() blobs for the mod-list and plugin-list
    // views, so manually-resized column widths survive a restart. Stored as base64
    // in config.json. Empty until the user first resizes a column.
    const QByteArray& modListHeaderState() const   { return m_modListHeaderState; }
    void setModListHeaderState(const QByteArray& v) { m_modListHeaderState = v; }
    const QByteArray& pluginListHeaderState() const { return m_pluginListHeaderState; }
    void setPluginListHeaderState(const QByteArray& v) { m_pluginListHeaderState = v; }

    static QString dataRoot();   // ~/.local/share/solero
    // Per-profile Overwrite capture dir: dataRoot()/overwrite/<sanitized profileName>.
    // Falls back to the legacy global dataRoot()/overwrite when profileName is empty.
    static QString overwriteDir(const QString& profileName);
    static QString configPath();
    static QStringList detectSkyrimPaths();
    // Pure parser: extract every Steam library "path" value from the textual VDF
    // contents of steamapps/libraryfolders.vdf. Returns library root paths (the
    // dir that contains steamapps/). Tolerant of both the legacy flat form and the
    // modern nested ("libraryfolders" -> "0" -> {"path": ...}) form. Exposed for
    // unit testing.
    static QStringList parseLibraryFoldersVdf(const QString& vdfContents);

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
    bool m_shaderCacheDeclined = false;
    bool m_toolsMigratedToPerProfile = false;
    bool m_outputModsProfileQualified = false;
    bool m_cycleSeparatorColors = true;
    bool m_dataShowAllFiles = false;
    bool m_promptAfterBrowserDownload = true;
    bool m_autoDeployBeforeLaunch = false;
    bool m_infoPanelVisible = true;
    bool m_autoCheckUpdates = true;
    bool m_verboseNextLaunch = false;
    qint64 m_lastUpdateCheckEpoch = 0;
    QString m_lastSeparatorColor;
    QString m_jackifyEnginePath;
    DeployMode m_deployMode = DeployMode::HardLink;
    QString m_themeMode = "system";
    QString m_accentColor;
    QString m_fontFamily;
    int m_fontSize = 0;
    QString m_lastProfile;
    QString m_preferredDownloadServer;
    QStringList m_cachedDownloadServers;
    QList<int> m_hiddenColumns;
    QByteArray m_modListHeaderState;
    QByteArray m_pluginListHeaderState;
};

} // namespace solero
