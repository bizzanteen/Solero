#pragma once
#include <QString>
#include <QByteArray>
#include <QList>
#include "deploy/DeployMode.h"

namespace solero {

class AppConfig {
public:
    static QString skyrimSeGame() { return QStringLiteral("skyrimspecialedition"); }
    static QString oblivionRemasteredGame() { return QStringLiteral("oblivionremastered"); }
    static QString gameDisplayName(const QString& game);
    static QString gameInstallDirectory(const QString& game);
    static QString gameExecutable(const QString& game);
    static QString gameAppId(const QString& game);
    static QString gameInstallMarker(const QString& game);
    static QString gameDataRelativePath(const QString& game);

    static AppConfig& instance();

    bool isConfigured() const;
    bool load();
    bool save() const;

    const QString& gameDir() const     { return m_gameDir; }
    const QString& gameType() const    { return m_gameType; }
    void setGameType(const QString& game);
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
    // One-time migration of the legacy global tool template into the active
    // profile's per-profile executables. Set true after the migration runs once.
    bool toolsMigratedToPerProfile() const    { return m_toolsMigratedToPerProfile; }
    void setToolsMigratedToPerProfile(bool v) { m_toolsMigratedToPerProfile = v; }
    // One-time migration that renames existing output-mod staging folders to be
    // profile-qualified ("<Profile> - <name>") so two profiles no longer share a
    // single bare folder. Set true after the migration runs once.
    bool outputModsProfileQualified() const   { return m_outputModsProfileQualified; }
    void setOutputModsProfileQualified(bool v){ m_outputModsProfileQualified = v; }
    // One-time repair of mods staged before extraction expanded Windows path
    // separators (files literally named "SKSE\\Plugins\\foo.dll"). Set true after
    // the repair runs once.
    bool stagingWindowsPathsRepaired() const  { return m_stagingWindowsPathsRepaired; }
    void setStagingWindowsPathsRepaired(bool v){ m_stagingWindowsPathsRepaired = v; }
    // One-time grouping of mods installed before auto-grouping worked across
    // separators. Set true after the pass runs once.
    bool multiFileModsGrouped() const         { return m_multiFileModsGrouped; }
    void setMultiFileModsGrouped(bool v)      { m_multiFileModsGrouped = v; }
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
    // Archives whose installed mod has since been deleted from Solero. The
    // Downloads tab shows these as "Mod deleted" instead of plain "Not installed";
    // reinstalling from the archive (or deleting the archive) clears the marker.
    const QStringList& removedModArchives() const     { return m_removedModArchives; }
    void setRemovedModArchives(const QStringList& v)  { m_removedModArchives = v; }
    // Record archives whose mod was just deleted; persists immediately.
    void markModArchivesRemoved(const QStringList& archives);
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
    const QByteArray& downloadsHeaderState() const   { return m_downloadsHeaderState; }
    void setDownloadsHeaderState(const QByteArray& v) { m_downloadsHeaderState = v; }
    const QByteArray& savesHeaderState() const       { return m_savesHeaderState; }
    void setSavesHeaderState(const QByteArray& v)     { m_savesHeaderState = v; }
    // App zoom factor the header-state blobs were saved under. saveState() stores
    // absolute pixel widths, so restoring at a different zoom leaves columns out of
    // step with the (zoom-scaled) font; views rescale restored widths by
    // currentZoom / headerStateZoom. Updated to the live zoom on every header save.
    double headerStateZoom() const                   { return m_headerStateZoom; }
    void setHeaderStateZoom(double z)                { m_headerStateZoom = z; }
    // Bumped when the default column layout changes, so a one-time migration can reset
    // stale persisted header widths and let the new defaults apply.
    int  columnLayoutVersion() const                 { return m_columnLayoutVersion; }
    void setColumnLayoutVersion(int v)               { m_columnLayoutVersion = v; }

    static QString dataRoot();   // ~/.local/share/solero
    // Per-profile Overwrite capture dir: dataRoot()/overwrite/<sanitized profileName>.
    // Falls back to the legacy global dataRoot()/overwrite when profileName is empty.
    static QString overwriteDir(const QString& profileName);
    static QString configPath();
    static QStringList detectSkyrimPaths();
    static QStringList detectGamePaths(const QString& game);
    // Library roots (the dir containing steamapps/) out of libraryfolders.vdf.
    // Handles both the flat and the nested ("libraryfolders" -> "0" -> path) forms.
    static QStringList parseLibraryFoldersVdf(const QString& vdfContents);

    // Derive the Proton-prefix local appdata + documents dirs from a game dir
    // (Steam prefix and game appid). Returns empty strings if not found.
    static QString detectLocalAppData(const QString& gameDir, const QString& game);
    static QString detectDocumentsDir(const QString& gameDir, const QString& game);

    // Find the Proton directory Skyrim uses (prefer GE). Empty if none found.
    QString detectProtonDir() const;

private:
    AppConfig() = default;
    QString m_gameDir;
    QString m_gameType = "skyrimspecialedition";
    QString m_stagingDir;
    QString m_dataDir; // derived: gameDir/Data
    QString m_localAppData;
    QString m_documents;
    QString m_downloads;
    bool m_confirmModDeletion = true;
    bool m_toolsMigratedToPerProfile = false;
    bool m_outputModsProfileQualified = false;
    bool m_stagingWindowsPathsRepaired = false;
    bool m_multiFileModsGrouped = false;
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
    QStringList m_removedModArchives;
    QList<int> m_hiddenColumns;
    QByteArray m_modListHeaderState;
    QByteArray m_pluginListHeaderState;
    QByteArray m_downloadsHeaderState;
    QByteArray m_savesHeaderState;
    double m_headerStateZoom = 1.0;
    int        m_columnLayoutVersion = 0;
};

} // namespace solero
