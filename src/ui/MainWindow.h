#pragma once
#include <QMainWindow>
#include <QAction>
#include <QPair>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QPointer>
#include <QList>
#include "core/ProfileManager.h"
#include "ai/AITransaction.h"
#include "ipc/IPCServer.h"
#include "deploy/DeployEngine.h"
#include "deploy/DeployMode.h"
#include "deploy/ConflictIndex.h"
#include "tools/ToolStore.h"
#include "tools/ToolRunner.h"
#include "ui/ExecutableDialog.h"
#include "nexus/DownloadManager.h"
#include "nexus/NxmHandler.h"
#include "import/Mo2Importer.h"

class QSplitter;
class QComboBox;
class QToolButton;
class QLabel;
class QTabWidget;
class QMenu;
class QPushButton;
class QStackedWidget;

namespace solero {
struct ModEntry;
class NexusWebView;
class ModListView;
class PluginListView;
class RightPane;
class BottomPanel;
class BethiniWindow;
class ProblemsDialog;
class RequirementsDialog;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

public slots:
    void handleNxmUrl(const QString& url);

private slots:
    // nxm from the embedded Nexus browser: enqueue without forcibly leaving the
    // browser view; optionally prompt whether to switch to the Downloads tab.
    void onBrowserNxmDownload(const QString& url);

signals:
    void conflictsUpdated(const solero::ConflictIndex& index);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    // Detach every model from the active Profile so a late paint can't deref it
    // once the ProfileManager (which owns the Profile) is gone. Idempotent.
    void detachProfileFromViews();

private:
    void setupToolbar();
    void setupCentralWidget();
    void switchProfile(const QString& name);
    // After a profile is newly created (manual, Wabbajack, or MO2 import), ask
    // whether to switch to it. Yes -> switchProfile(newName); No -> keep the
    // current profile (the new one only appears in the dropdown). The first
    // profile (no active profile yet) switches immediately without a prompt.
    // priorActiveName names the profile that was active *before* the create; it
    // matters only for the import path, where the importer already made the new
    // profile active and freed the old one, so a "No" must reload priorActiveName.
    void promptSwitchToNewProfile(const QString& newName,
                                  const QString& priorActiveName = QString());
    void refreshProfileCombo();
    void onDeployToggle();
    bool deployCurrent();        // perform a deploy of the active profile; returns success
    // Like deployCurrent but for use INSIDE a tool run: deploys the active profile
    // (honoring any in-memory ModList toggle) under a ProgressModal and updates
    // deploy/conflict state, without running post-deploy tools or popping the
    // AppData warning. Returns deploy success. Used to push the output-mod toggle
    // through to the deployed game Data around PGPatcher/Radium runs.
    bool redeployForTool(const QString& title, const QString& message);
    void showRunLock(const QString& toolName);
    void hideRunLock();
    void refreshDeployState();   // detect an existing deployment on startup
    void updateDeployButton();   // sync the toggle's text/tooltip to m_deployed
    // Recompute the aggregated health issues for the active profile and refresh
    // the toolbar Problems indicator (count + worst-severity icon). Cheap enough
    // to call from the existing post-deploy / profile-switch / scan refresh points
    // (a shallow dependency scan, no recursion); not wired to per-keystroke events.
    void refreshHealthIndicator();
    void onShowProblems();       // open (or refresh) the non-modal Problems panel
    // Data-tab rename/delete of a staged file or folder, applied to the mod's
    // staging dir (stagingDir/<modId>/<relPath>).
    void onDataRename(const QString& modId, const QString& relPath,
                      const QString& newName, bool isFolder);
    void onDataDelete(const QString& modId, const QString& relPath, bool isFolder);
    void updatePluginNotice();   // show/hide the Plugins-tab staleness notice
    void updateSortButton();     // enable "Sort Now" only when deployed && load order dirty
    void onLoadOrderChanged();   // user manually reordered a plugin -> mark order dirty
    void onSortRequested();      // run LOOT on the current load order
    void onLockOrderToggled(bool checked); // lock/unlock the profile's load order
    void onBackupLo();           // snapshot the current load order to <profile>/lo-backups
    void onRestoreLo();          // pick a snapshot and restore it (MO2-style reconcile)
    void onNewProfile();
    void onDeleteProfile();
    void onRenameProfile();
    void onImportMo2();
    void onExportProfile();      // write the active profile to a .solero-profile.json manifest
    void onImportProfile();      // reconstruct a profile from a .solero-profile.json manifest
    void onInstallWabbajack();
    // Refresh the combo and prompt to switch to a freshly imported profile.
    // The importer has already made `name` the active profile (freeing the old
    // one), so callers pass priorActiveName - the profile that was active before
    // the import - so a "No" answer can reload it.
    void selectImportedProfile(const QString& name, const QString& priorActiveName);
    void onInstallMod();
    void onToggleNexus(bool on);
    // Shared resolve-and-enqueue for both nxm paths. Returns the saved filename
    // on success (for status/prompt text), or an empty string on failure.
    QString startNxmDownload(const QString& url);
    void onNexusDownload(const QString& modId, const QString& fileId,
                         const QString& fileName, const QString& version);
    void installFromArchive(const QString& archive);
    void onReinstallMod(const QString& modId);
    // "Create Mod from Overwrite…": prompt for a name, move the overwrite dir's
    // contents into a new mod's staging Data, append it at the bottom of the load
    // order, and mark the deployment dirty.
    void onCreateModFromOverwrite();
    // "Clear Shader Cache" context action on the Community Shaders mod: confirm,
    // then wipe the live + overwrite + managed-cache copies of Data/ShaderCache.
    void onClearShaderCache(const QString& modId);
    // One-time offer: if CS is present, the cache isn't already managed, and the
    // user hasn't previously declined, ask whether Solero should manage the cache.
    void maybeOfferShaderCacheManagement();
    // Turn on managed shader caching for the active profile: set Profile::shaderCache
    // (a unique staging folder) and stage its empty Data/ShaderCache folder.
    void enableManagedCache();
    // "Redownload from Nexus" context action: re-fetch the mod's exact archive
    // (Premium -> enqueue into downloadsDir; free/unavailable -> nxm guidance).
    void onRedownloadMod(const QString& modId);
    // Downloads tab "Retry" action: re-resolve an expiring Nexus CDN URL (or reuse
    // a stable direct URL) and re-enqueue the failed download.
    void onRetryDownload(const QString& fileName);
    // Reinstall helper: candidate archives in downloadsDir for a mod, in priority
    // order (stored sourceArchive basename -> Nexus sidecar modId/fileId -> fuzzy
    // name). De-duplicated; empty when nothing plausible is found.
    QStringList findDownloadArchivesFor(const solero::ModEntry* existing) const;
    // Generate a name like "<base> (2)" that doesn't collide with any mod in the
    // profile (case-insensitive). Returns base unchanged if it's already free.
    QString uniqueModName(const QString& base, solero::Profile* profile) const;
    // Resolve a mod id to its on-disk staging root (stagingDir/<stagingFolder>,
    // falling back to the id). Uses the active profile's mod list.
    QString stagingRootForId(const QString& modId) const;
    void onEndorseMod(const QString& modId);
    void onViewNexusPage(const QString& modId);
    void onUpdateMod(const QString& modId);
    void onCheckUpdates();
    // Walk the active profile's enabled mods, locate each source archive, detect
    // FOMOD (fomod/ModuleConfig.xml), set sourceArchive + isFomod, and back-fill
    // fomod-choices.json where the selection is reconstructable. Runs synchronously
    // behind a cancellable ProgressModal (it extracts ~dozens of archives).
    void onScanFomod();
    // Launch the async (off-UI-thread) Nexus update check. silentIfNone=true is
    // used by the auto-check-on-profile-load path: it no-ops quietly when there's
    // no API key, no mods with Nexus metadata, or a check is already running.
    void runUpdateCheck(bool silentIfNone);
    // After a profile finishes loading, fire an auto update check if enabled and
    // not throttled (>6h since the last check) and a key is available.
    void maybeAutoCheckUpdates();
    // Ensure the mod has Nexus mod/file ids. Returns true if known (or resolved
    // via MD5 archive lookup). Shows an explanatory message + returns false when
    // it can't (no source archive, no Nexus match). Used by re-download.
    bool ensureNexusIds(solero::ModEntry* mod);
    // Single canonical "no Nexus key" gate. Returns true if a key is configured;
    // otherwise shows one dialog pointing the user to Settings -> Nexus Account
    // (with a button that opens Settings on the Nexus tab) and returns false.
    // `context` is a short phrase like "download updates" for the message.
    bool requireNexusKey(const QString& context);
    // Open the Settings dialog (wired to the Connect-to-Nexus browser flow).
    void openSettingsDialog();
    // Shared "deploy required before launching" gate. Returns true if it's safe to
    // proceed (already deployed, or the user chose to deploy now / always). Honors
    // AppConfig::autoDeployBeforeLaunch (silent deploy + skip the modal). `reason`
    // is a short phrase shown in the prompt (e.g. "launching the game").
    bool ensureDeployed(const QString& reason);
    // SKSE bootstrap: after a Wabbajack import, detect whether SKSE64 is present
    // and, if not, offer to install it from Nexus (mod 30379, Steam build).
    bool skseInstalledFor(solero::Profile* profile) const;
    // Silently install the latest SKSE for the active profile when it's missing
    // (no prompt). No-ops if SKSE is present, a download is in flight, or there's
    // no Nexus key/network (deferred - retried on next launch/profile switch).
    void ensureSkse();
    void installSkseFromNexus();                 // resolve + install the latest main build
    void installSkseVersion(const QString& fileId, const QString& version); // a specific build
    // SKSE version currently installed for the active profile (empty if none).
    QString installedSkseVersion(solero::Profile* profile) const;
    // One-time: move any legacy global Overwrite content (dataRoot/overwrite/*) that
    // isn't already a per-profile subdir into the active profile's Overwrite dir.
    void migrateLegacyOverwrite();
    // After a Nexus mod installs, query its requirements and, for any that aren't
    // already installed, offer to install them (placed just above the dependent mod).
    void checkRequirementsAfterInstall(solero::Profile* profile,
                                       const QString& dependentModId,
                                       const QString& nexusModId, const QString& game);
    // Resolve a required mod's main file and download+install it via the auto-install
    // path, recording that it should land directly above dependentModId.
    void downloadRequirement(const QString& reqModId, const QString& reqName,
                             const QString& dependentModId, const QString& game);
    void onModsChanged();
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();
    void onRunTool(const solero::Executable& exe);
    // After a successful deploy, run each tool flagged "Run on deployment"
    // (runThroughDeployer) in listed order via ToolRunner, honoring the UI lock.
    void runPostDeployTools();
    void onAddTool2();
    QString ensureOutputMod(const QString& name);
    // Pick a tool's output mod: suggest an existing load-order mod whose name
    // matches (so imported lists' output mods are reused), else create one.
    QString chooseOutputMod(const QString& defaultName, const QString& toolName);
    // Auto-configure a freshly-imported Wabbajack modlist's tools (discovered from
    // ModOrganizer.ini [customExecutables]) against the now-active profile: map
    // each to a ToolCatalog preset (or register a custom executable), wire output
    // mods, add to the global ToolStore, then show a summary. Idempotent: skips
    // tools already present in the ToolStore.
    void setUpImportedTools(const QList<solero::ImportedTool>& tools);
    // Tools are global but their output mods live in whichever profile was active
    // at setup - so resolve/remove a mod across all profiles, not just the active one.
    QString modNameAnywhere(const QString& id) const;
    void removeModEverywhere(const QString& id);
    void onEditTool(const QString& id);
    void onRemoveTool(const QString& id);
    void onManageTools();
    // The tools the user sees/runs/edits live on the active profile (the global
    // m_toolStore is only a template/library). activeTools() returns the active
    // profile's executables() (or a static empty list if there's no active
    // profile - defensive); saveActiveTools() persists them via Profile::save().
    QList<solero::Executable>& activeTools();
    void saveActiveTools();
    // one-time migration (gated by AppConfig::toolsMigratedToPerProfile): fold the
    // legacy global tool template (m_toolStore) into the active profile only,
    // re-resolving each tool's output mod to this profile's same-named output mod
    // (or empty to defer creation). New/other profiles are never seeded.
    void migrateToolsToActiveProfileOnce();
    void onPatchWizard();
    QList<QPair<QString,QString>> modChoices() const;
    void rebuildToolsMenu();
    void onOpenBethini();
    void onOpenLootRules();
    void onPlay();

    solero::ProfileManager* m_profileMgr;
    solero::AITransactionLog* m_txLog;
    solero::IPCServer* m_ipcServer;

    bool m_deployed = false;
    bool m_deployDirty = false;
    bool m_loadOrderDirty = false; // user manually reordered plugins since last sort/deploy
    bool m_toolRunning = false;  // guards re-entrancy while a tool runs (nested event loop)
    bool m_switchingProfile = false; // guards re-entrant switchProfile (pumps re-dispatch combo changes)
    bool m_warnedMissingAppData = false; // one-time warning when AppData can't be located
    QAction* m_deployAction = nullptr;
    QToolButton* m_problemsBtn = nullptr;          // toolbar health indicator
    solero::ProblemsDialog* m_problemsDialog = nullptr;
    QString m_lastDeployWarning;                   // last DeployResult::warning
    solero::ConflictIndex m_lastConflicts;         // last deployed/loaded conflict index
    QComboBox* m_profileCombo = nullptr;
    QSplitter* m_splitter = nullptr;
    solero::ModListView*    m_modListView = nullptr;
    solero::RightPane*      m_rightPane = nullptr;
    solero::BottomPanel*    m_bottomPanel = nullptr;
    solero::BethiniWindow*  m_bethiniWindow = nullptr;
    QToolButton* m_toolsBtn = nullptr;
    QMenu* m_toolsMenu = nullptr;
    solero::ToolStore* m_toolStore = nullptr;
    solero::DownloadManager* m_downloads = nullptr;
    solero::NexusWebView* m_nexusWeb = nullptr;
    QStackedWidget* m_centralStack = nullptr;
    QWidget* m_modManagerPage = nullptr;
    QAction* m_browseAction = nullptr;
    QAction* m_checkUpdatesAction = nullptr;
    // Receives the result of the off-thread update check (local id -> {installed, latest}).
    QFutureWatcher<QHash<QString, QPair<QString,QString>>> m_updateWatcher;

    // Per-mod "Update Mod" flow: resolve the latest Nexus file off the UI thread,
    // then download it and reinstall the existing mod in place.
    struct ResolvedUpdate { QString url, fileName, fileId, version; bool ok = false; QString error; };
    QFutureWatcher<ResolvedUpdate> m_updateResolveWatcher;
    // Local id + display name of the mod whose update is currently being resolved.
    // Single-flight: guarded by m_updateResolveWatcher.isRunning().
    QString m_updateTargetId, m_updateTargetName;
    // In-flight update downloads, keyed by saved filename. When such a download
    // finishes, the existing mod is reinstalled in place instead of added anew.
    struct PendingUpdate { QString modId, fileId, version; };
    QHash<QString, PendingUpdate> m_pendingUpdates;
    // In-flight reinstall downloads (archive was missing on disk), keyed by saved
    // filename -> local modId. When the download finishes, onReinstallMod is
    // re-invoked; the archive is now present (located by its Nexus sidecar).
    QHash<QString, QString> m_pendingReinstalls;
    // Whether the once-per-launch update check has run yet (bypasses the 6h throttle).
    bool m_didLaunchUpdateCheck = false;
    // Pending Nexus metadata for in-flight nxm downloads, keyed by saved filename.
    // Written to a <archive>.solero-nexus.json sidecar when the download finishes.
    QHash<QString, QJsonObject> m_nxmMeta;
    // A download that failed (not cancelled). Retained so the Downloads tab can
    // show a "Failed" row and the user can retry. For Nexus downloads `meta`
    // carries {game, modId, fileId, version} so retry can re-resolve the expiring
    // CDN URL; for direct/tool downloads `url` is the stable link to reuse.
    struct FailedDownload { QString fileName; QJsonObject meta; QString error; };
    QList<FailedDownload> m_failedDownloads;

    // SKSE-from-Nexus: resolve the download off the UI thread, then auto-install.
    struct ResolvedSkse { QString url, fileName, fileId, version; bool ok = false; QString error; };
    QFutureWatcher<ResolvedSkse> m_skseResolveWatcher;
    bool m_skseInstalling = false; // guard against re-offering while a SKSE DL is pending
    // Filenames flagged to auto-install once their download finishes (e.g. SKSE).
    QSet<QString> m_autoInstall;
    // requirement's Nexus modId -> the dependent mod's id it should be placed above,
    // applied when the requirement finishes installing (see checkRequirementsAfterInstall).
    QHash<QString, QString> m_placeAboveByModId;
    // Open requirements dialogs (modal, but can stack via nested exec()). Lets an
    // async install flip the matching row to Installed / Retry when it finishes.
    QList<QPointer<solero::RequirementsDialog>> m_openReqDialogs;
    void markRequirementResult(const QString& reqModId, bool installed);

    QWidget* m_runOverlay = nullptr;
    QLabel* m_runLockLabel = nullptr;
    QPushButton* m_unlockBtn = nullptr;

    QString profilesRoot() const;
    QString txLogPath() const;
};
