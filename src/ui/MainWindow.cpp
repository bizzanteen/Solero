#include "MainWindow.h"
#include "ModListView.h"
#include "RightPane.h"
#include "DownloadsTab.h"
#include "BottomPanel.h"
#include "SetupWizard.h"
#include "ui/SettingsDialog.h"
#include "bethini/BethiniWindow.h"
#include "app/Application.h"
#include "core/AppConfig.h"
#include "core/ModList.h"
#include "core/StagingFolder.h"
#include "core/OutputModMigration.h"
#include "core/FileUtil.h"
#include "core/ShaderCache.h"
#include "core/VersionUtil.h"
#include "core/LoadOrderBackup.h"
#include "core/FileMove.h"
#include "install/ModInstaller.h"
#include "install/InstallConflict.h"
#include "import/Mo2Importer.h"
#include "io/ProfileManifest.h"
#include "ui/WabbajackDialog.h"
#include "ui/FomodWizard.h"
#include "fomod/FomodChoiceRecall.h"
#include "ui/ProgressModal.h"
#include "ui/ToolSetupWizard.h"
#include "ui/ExecutableDialog.h"
#include "ui/ToolsManagerDialog.h"
#include "ui/NexusWebView.h"
#include "ui/RequirementsDialog.h"
#include "ui/LootRulesEditor.h"
#include "ui/PatchWizardDialog.h"
#include "tools/ToolRunner.h"
#include "tools/ToolCatalog.h"
#include "tools/ToolNameMap.h"
#include "tools/ToolSetup.h"
#include "tools/RadiumPrep.h"
#include "tools/PgpatcherConfig.h"
#include "fomod/FomodEngine.h"
#include "loot/LootSorter.h"
#include "PluginListView.h"
#include "ui/ProblemsDialog.h"
#include "ui/KeyboardShortcutsDialog.h"
#include "ui/ReportDialog.h"
#include "ui/IconUtil.h"
#include "core/HealthCheck.h"
#include "core/Log.h"
#include "install/DependencyChecker.h"
#include "nexus/NexusApi.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QEventLoop>
#include <QTabWidget>
#include <QStackedWidget>
#include <QSplitter>
#include <QComboBox>
#include <QToolBar>
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QElapsedTimer>
#include <QThread>
#include <QProcess>
#include <QKeySequence>
#include <QActionGroup>
#include <QShortcut>
#include <QStandardPaths>
#include <QMessageBox>
#include <QScopeGuard>
#include <QAbstractButton>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QPair>
#include <QMenu>
#include <QMenuBar>
#include <QFont>
#include <QPalette>
#include <QSizePolicy>
#include <QIcon>
#include <QStatusBar>
#include <QTimer>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QApplication>
#include <QThreadPool>
#include <QMutex>
#include <QPointer>
#include <atomic>
#include <QFile>
#include <QCryptographicHash>
#include <algorithm>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileDialog>
#include <QUuid>
#include <QDesktopServices>
#include <QUrl>
#include <memory>

namespace {
using solero::isVersionNewer;   // robust version compare (handles MO2 ".0SE"/".0c" tails)
using solero::normalizeVersion;

// Returns true when a string looks suitable for direct display to the user:
// not a raw file path, not a Qt/process debug string.
static bool looksHumanReadable(const QString& s) {
    // Reject Windows/Wine-style drive paths: "C:\..." or "C:/..."
    const bool hasDrivePath = s.length() >= 3
        && s[0].isLetter()
        && (s[1] == QLatin1Char(':'))
        && (s[2] == QLatin1Char('\\') || s[2] == QLatin1Char('/'));
    return !s.isEmpty()
        && !s.startsWith(QLatin1Char('/'))
        && !hasDrivePath
        && !s.contains(QLatin1String("exit code"), Qt::CaseInsensitive)
        && !s.contains(QLatin1String("errno"), Qt::CaseInsensitive);
}

// Strips a leading file-path prefix from LOOT exception strings, e.g.
// "/path/to/Plugin.esm: Cyclic interaction ..." -> "Cyclic interaction ..."
static QString stripLootPathPrefix(const QString& msg) {
    if (msg.startsWith(QLatin1Char('/'))) {
        const int colon = msg.indexOf(QLatin1String(": "));
        if (colon > 0) return msg.mid(colon + 2);
    }
    return msg;
}

// Nexus archive names look like "<name>-<modId>-<version-parts>-<timestamp>"
// (e.g. "SkyUI_5_2_SE-12604-5-2-SE-1462810437"). When we know the exact modId
// from the Nexus sidecar, strip everything from that "-<modId>" boundary on so
// the displayed name reads just "<name>". For manually-added archives (no
// sidecar / empty modId) leave the name untouched - guessing would be too risky.
// Normalize a name for fuzzy/identity comparison: lowercase, alphanumerics only.
QString normalizeName(const QString& s) {
    QString r;
    for (QChar c : s) if (c.isLetterOrNumber()) r += c.toLower();
    return r;
}

// True iff the tool's output mod has at least one staged file under its Data/.
// On first run (nothing produced yet) the dir is empty/absent - return false so
// the disable+redeploy dance is skipped (there's nothing in the deployed Data to
// remove). p may be null; modId may be empty.
bool outputModHasStagedFiles(solero::Profile* p, const QString& modId) {
    if (!p || modId.isEmpty()) return false;
    const QString dir = solero::AppConfig::instance().stagingDir() + "/" +
                        p->stagingFolderFor(modId) + "/Data";
    return solero::dirHasFiles(dir);
}

QString cleanModName(const QString& raw, const QString& nexusModId) {
    if (nexusModId.isEmpty())
        return raw;
    QRegularExpression re("-" + QRegularExpression::escape(nexusModId) + "(?:-|$)");
    QRegularExpressionMatch m = re.match(raw);
    if (!m.hasMatch())
        return raw;
    QString cut = raw.left(m.capturedStart());
    // Trim a trailing run of separators/whitespace left behind by the cut.
    cut.replace(QRegularExpression("[-_\\s]+$"), QString());
    cut = cut.trimmed();
    return cut.isEmpty() ? raw : cut;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Solero");
    resize(1280, 800);

    // Load global config; show setup wizard if not yet configured
    solero::AppConfig::instance().load();
    if (!solero::AppConfig::instance().isConfigured()) {
        solero::SetupWizard wizard(this);
        if (wizard.exec() != QDialog::Accepted) {
            // User cancelled - quit
            QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            return;
        }
    }

    QString root = profilesRoot();
    m_profileMgr = new solero::ProfileManager(root);
    m_txLog = new solero::AITransactionLog(txLogPath());
    m_toolStore = new solero::ToolStore(solero::ToolStore::defaultPath());

    if (m_profileMgr->profileNames().isEmpty())
        m_profileMgr->createProfile("Default");

    m_ipcServer = new solero::IPCServer(this);
    m_ipcServer->setTransactionLog(m_txLog);
    m_ipcServer->start("solero-ipc");

    setupMenuBar();
    setupToolbar();
    setupStatusBar();
    setupCentralWidget();
    setAcceptDrops(true); // drop archive files onto the window to install
    statusBar()->showMessage("Ready");

    // When the off-thread update check finishes, apply results on the UI thread.
    connect(&m_updateWatcher, &QFutureWatcher<UpdateScan>::finished, this, [this]() {
        const UpdateScan scan = m_updateWatcher.result();
        // Always clear the inline progress and re-arm the action, even for a
        // stale scan - the check is over either way.
        if (m_bottomPanel) m_bottomPanel->setUpdateProgress(-1, 0);
        if (m_checkUpdatesAction) m_checkUpdatesAction->setEnabled(true);
        // Discard results from a superseded generation: the profile switched (or a
        // newer check started) while this one was in flight, so its updates and
        // backfill belong to a profile that is no longer active.
        if (scan.gen != m_updateGen) return;
        // A deploy is running off-thread and is reading/writing this same profile; do
        // not mutate or save it now. Stash the result and apply it when the
        // deploy finishes (finishDeployJob).
        if (m_deploying) {
            m_pendingUpdateScan = scan;
            m_hasPendingUpdateScan = true;
            return;
        }
        applyUpdateScan(scan);
    });

    // off-thread deploy finished. The worker only linked files (never
    // touched a widget); all result handling runs here, on the UI thread. This is
    // the exact tail of the old synchronous deployCurrent(), moved into the watcher
    // so the deploy itself no longer blocks the event loop.
    connect(&m_deployWatcher, &QFutureWatcher<solero::DeployResult>::finished, this, [this]() {
        const solero::DeployResult result = m_deployWatcher.result();
        finishDeployJob(); // close modal, clear guard, restore controls + repaints
        auto* profile = m_profileMgr->activeProfile();

        if (!result.success)
            QMessageBox::warning(this, "Deploy Incomplete", result.errorMessage);
        if (!result.warning.isEmpty())
            QMessageBox::information(this, "Deploy Notice", result.warning);
        m_lastDeployWarning = result.warning;
        m_lastDeployHadFailures = !result.success;
        m_lastDeployError = result.success ? QString() : result.errorMessage;

        m_deployed = result.success;
        m_deployDirty = false;
        statusBar()->showMessage(
            QString("Deployed %1 files. %2 conflicts. Plugins sorted by LOOT.")
                .arg(result.filesDeployed)
                .arg(result.conflicts.conflictedPaths().size()));
        m_lastConflicts = result.conflicts;
        m_rightPane->setConflictIndex(result.conflicts);
        m_modListView->setConflictIndex(result.conflicts);
        if (profile) m_rightPane->setProfile(profile); // LOOT may have reordered plugins
        m_modListView->invalidateModCache();           // deploy may have populated Overwrite
        emit conflictsUpdated(result.conflicts);
        m_loadOrderDirty = false;                       // deploy auto-sorts via LOOT
        updateDeployButton();
        updatePluginNotice();
        updateSortButton();
        refreshHealthIndicator();

        // Run the finished continuation (used by deployCurrent's local event loop to
        // return a bool to Play / tool runs) before post-deploy tools, matching the
        // old synchronous ordering (deployCurrent returned after runPostDeployTools).
        auto cont = m_deployCont; m_deployCont = nullptr;
        if (result.success) runPostDeployTools(); // only after a real, full success
        if (cont) cont(result.success);
    });

    // off-thread undeploy finished (Deploy toolbar button's undeploy path).
    connect(&m_undeployWatcher, &QFutureWatcher<bool>::finished, this, [this]() {
        const bool undeployOk = m_undeployWatcher.result();
        finishDeployJob();
        if (!undeployOk)
            statusBar()->showMessage(
                "Undeploy finished with warnings - some deployed files may remain "
                "in the game folder.", 6000);
        m_deployed = false;
        m_deployDirty = false;
        m_loadOrderDirty = false;                       // not deployed -> manual sort n/a
        m_modListView->invalidateModCache();            // undeploy may have cleared Overwrite
        if (auto* p = m_profileMgr->activeProfile()) m_rightPane->refreshPlugins(p);
        m_lastConflicts.clear();                        // no live deployment -> no winners
        m_lastDeployWarning.clear();
        m_lastDeployHadFailures = false;
        if (undeployOk) statusBar()->showMessage("Undeployed.");
        updateDeployButton();
        updatePluginNotice();
        updateSortButton();
        refreshHealthIndicator();
        auto cont = m_undeployCont; m_undeployCont = nullptr;
        if (cont) cont(undeployOk);
    });

    // Per-mod update resolve finished: kick off the download (or report an error).
    connect(&m_updateResolveWatcher, &QFutureWatcher<ResolvedUpdate>::finished,
            this, [this]() {
        const ResolvedUpdate r = m_updateResolveWatcher.result();
        if (!r.ok) {
            QMessageBox::warning(this, "Update Mod",
                r.error.isEmpty() ? QString("Could not prepare an update for this mod.") : r.error);
            statusBar()->showMessage("Ready");
            return;
        }
        // If the resolved version matches what's already installed, there's nothing
        // to do - don't download/install. Compare case-insensitively, trimmed.
        if (auto* profile = m_profileMgr->activeProfile()) {
            if (solero::ModEntry* target = profile->modList().findById(m_updateTargetId)) {
                const QString resolvedV = r.version.trimmed();
                const QString currentV  = target->version.trimmed();
                // Skip if the resolved version isn't actually newer (handles exact
                // matches and trailing-zero padding like 1.0.1.0 vs 1.0.1).
                if (!resolvedV.isEmpty() && !currentV.isEmpty() &&
                    !isVersionNewer(currentV, resolvedV)) {
                    statusBar()->showMessage(
                        m_updateTargetName + " is already up to date (version " + currentV + ").");
                    return;
                }
            }
        }
        // Record the pending update so the download-finished handler reinstalls
        // the existing mod in place rather than adding a new one.
        m_pendingUpdates[r.fileName] = PendingUpdate{m_updateTargetId, r.fileId, r.version};
        m_downloads->enqueue(r.url, r.fileName, solero::AppConfig::instance().downloadsDir());
        m_rightPane->showDownloadsTab();
        statusBar()->showMessage("Downloading update for " + m_updateTargetName + "\xe2\x80\xa6");
    });

    // SKSE resolve finished: write the Nexus sidecar, flag for auto-install, and
    // enqueue the download. The download-finished handler installs it as a mod.
    connect(&m_skseResolveWatcher, &QFutureWatcher<ResolvedSkse>::finished,
            this, [this]() {
        const ResolvedSkse r = m_skseResolveWatcher.result();
        if (!r.ok || r.url.isEmpty()) {
            m_skseInstalling = false;
            QMessageBox::warning(this, "Install SKSE",
                r.error.isEmpty() ? QString("Could not prepare the SKSE download.") : r.error);
            statusBar()->showMessage("Ready");
            return;
        }
        // Tag the installed mod with SKSE's Nexus identity so thumbnail/version/
        // update tracking work (installFromArchive reads this sidecar).
        m_nxmMeta[r.fileName] = QJsonObject{
            {"game",  "skyrimspecialedition"},
            {"modId", "30379"},
            {"fileId", r.fileId},
            {"version", r.version},
        };
        m_autoInstall.insert(r.fileName);
        m_downloads->enqueue(r.url, r.fileName, solero::AppConfig::instance().downloadsDir());
        m_rightPane->showDownloadsTab();
        statusBar()->showMessage("Downloading SKSE\xe2\x80\xa6");
    });

    // Restore the profile active at last close; fall back to the first profile
    // (alphabetical) only when none was saved or it no longer exists on disk.
    const QStringList profileNames = m_profileMgr->profileNames();
    const QString savedProfile = solero::AppConfig::instance().lastProfile();

    // one-time: profile-qualify existing output-mod staging folders so two
    // profiles no longer share a single bare folder (e.g. "PGPatcher Output").
    // Must run before switchProfile so the now-active profile reads the migrated
    // modlist. Processes all profiles on disk; the last/active profile goes first
    // so it claims the shared folder's content (others get fresh empty folders).
    if (!solero::AppConfig::instance().outputModsProfileQualified()) {
        QStringList order = profileNames;
        if (profileNames.contains(savedProfile)) {
            order.removeAll(savedProfile);
            order.prepend(savedProfile);
        }
        solero::migrateOutputModsProfileQualified(
            profilesRoot(), solero::AppConfig::instance().stagingDir(), order);
        solero::AppConfig::instance().setOutputModsProfileQualified(true);
        solero::AppConfig::instance().save();
    }

    switchProfile(profileNames.contains(savedProfile) ? savedProfile : profileNames.first());
    // One-time: fold the legacy global tool template into the now-active profile
    // only (gated by toolsMigratedToPerProfile). Must run after switchProfile so
    // there's an active profile; rebuild the menu to show any migrated tools.
    migrateToolsToActiveProfileOnce();
    rebuildToolsMenu();
    migrateLegacyOverwrite();   // one-time: fold legacy global Overwrite into this profile
    refreshDeployState(); // reflect any existing deployment from a previous run

    // Retroactive one-time scan: if the loaded profile already has Community
    // Shaders but no managed cache, offer to manage it (deferred so the UI is
    // ready before any modal). No-op if declined before or already managed.
    QTimer::singleShot(0, this, &MainWindow::maybeOfferShaderCacheManagement);

    // If the previous run crashed (a marker was found at installLogging()), prompt the
    // crash-report form once the UI is up. Deferred so the window is shown first.
    QTimer::singleShot(0, this, &MainWindow::maybePromptCrashReport);
}

void MainWindow::detachProfileFromViews() {
    if (m_modListView)   m_modListView->setProfile(nullptr);
    if (m_rightPane)     m_rightPane->setProfile(nullptr, /*reconcilePlugins=*/false);
    if (m_bottomPanel)   m_bottomPanel->setProfile(nullptr);
    if (m_bethiniWindow) m_bethiniWindow->setProfile(nullptr);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Safety net: flush the active profile so any mutation path that forgot to
    // save() doesn't lose the change on exit. Never block quit on a failed save.
    if (auto* active = m_profileMgr->activeProfile()) {
        if (!active->save())
            qCWarning(lcProfile) << "closeEvent: safety-net save failed for profile"
                                 << active->name();
    }
    // Drop model->Profile references while the event loop is still alive, so a
    // queued paint after the window starts closing can't deref a freed Profile.
    detachProfileFromViews();
    QMainWindow::closeEvent(event);
}

MainWindow::~MainWindow() {
    // The models hold a raw Profile* owned by m_profileMgr; detach them before
    // freeing it, or a child view's late paint segfaults on the dangling pointer.
    detachProfileFromViews();
    delete m_profileMgr;
    delete m_txLog;
}

QString MainWindow::startNxmDownload(const QString& url) {
    if (!solero::AppConfig::instance().isConfigured()) {
        QMessageBox::warning(this, "Nexus Download", "Configure the game first (\xe2\x9a\x99 Settings).");
        return {};
    }
    auto link = solero::NxmHandler::parse(url);
    if (!link.valid) {
        // Distinguish a non-Skyrim game domain from a malformed link so the user
        // understands why an nxm://oblivion/... link won't download here.
        if (!link.game.isEmpty() && !solero::NxmHandler::isSupportedGame(link.game))
            QMessageBox::warning(this, "Nexus Download",
                QString("That link is for \"%1\", which Solero doesn't manage.\n"
                        "Solero only handles Skyrim Special Edition mods.").arg(link.game));
        else
            QMessageBox::warning(this, "Nexus Download",
                "Couldn't understand that Nexus link:\n" + url);
        return {};
    }

    // The resolve below does blocking network I/O on the UI thread (we keep the
    // working synchronous flow). Give the user a busy state and make sure the
    // cursor is always restored on every return path.
    struct CursorGuard {
        CursorGuard() { QApplication::setOverrideCursor(Qt::WaitCursor); }
        ~CursorGuard() { QApplication::restoreOverrideCursor(); }
    } cursorGuard;
    statusBar()->showMessage("Resolving Nexus download\xe2\x80\xa6"); qApp->processEvents();
    QString cdn = solero::NxmHandler::resolveDownloadUrl(link);
    if (cdn.isEmpty()) {
        QMessageBox::warning(this, "Nexus Download",
            "Could not resolve the download. The link may have expired, or a Nexus Premium API key may be required for this file.");
        return {};
    }
    QString fn = solero::NxmHandler::fileName(link);
    if (fn.isEmpty()) fn = "nexus-" + link.modId + "-" + link.fileId + ".archive";
    // Record Nexus metadata keyed by the saved filename; a sidecar is written when
    // the download finishes so the installed mod can carry mod/file/version ids.
    QJsonObject meta;
    meta["game"] = link.game;
    meta["modId"] = link.modId;
    meta["fileId"] = link.fileId;
    meta["version"] = solero::NexusApi::fileVersion(link.modId, link.fileId, link.game);
    m_nxmMeta[fn] = meta;

    // Guard against re-downloading something the user already has, either as an
    // installed mod (matched by Nexus mod/file id) or as an archive still sitting
    // in the downloads folder. Both are recoverable, so this is a confirm, not a block.
    auto* profile = m_profileMgr->activeProfile();
    const QString destPath = solero::AppConfig::instance().downloadsDir() + "/" + fn;
    const bool alreadyInstalled = profile &&
        profile->modList().findByNexusFile(link.modId, link.fileId) != nullptr;
    const bool alreadyOnDisk = QFileInfo::exists(destPath);
    if (alreadyInstalled || alreadyOnDisk) {
        const QString msg = alreadyInstalled
            ? QString("\"%1\" is already installed as a mod.").arg(fn)
            : QString("\"%1\" is already in your downloads folder.").arg(fn);
        if (QMessageBox::question(this, "Already have this file",
                msg + "\n\nDownload it again anyway?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return {};
    }

    m_downloads->enqueue(cdn, fn, solero::AppConfig::instance().downloadsDir());
    return fn;
}

void MainWindow::handleNxmUrl(const QString& url) {
    // External (OS protocol handler) path: the mod manager is the visible view,
    // so enqueue and surface the Downloads tab.
    raise(); activateWindow();
    QString fn = startNxmDownload(url);
    if (fn.isEmpty()) return;
    if (m_browseAction && m_browseAction->isChecked()) m_browseAction->setChecked(false);
    m_rightPane->showDownloadsTab();
    statusBar()->showMessage("Downloading " + fn + "\xe2\x80\xa6");
}

void MainWindow::onBrowserNxmDownload(const QString& url) {
    // Triggered from the embedded Nexus browser. The download starts in the
    // background; we deliberately do not force-switch away from the browser.
    QString fn = startNxmDownload(url);
    if (fn.isEmpty()) return;

    if (!solero::AppConfig::instance().promptAfterBrowserDownload()) {
        statusBar()->showMessage("Downloading " + fn + "\xe2\x80\xa6", 4000);
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle("Download started");
    box.setText("Started downloading " + fn + ".\nView it in the Downloads tab, or keep browsing?");
    QPushButton* viewBtn = box.addButton("View Downloads", QMessageBox::AcceptRole);
    QPushButton* keepBtn = box.addButton("Keep Browsing",  QMessageBox::RejectRole);
    box.setDefaultButton(keepBtn);
    QCheckBox* dontAsk = new QCheckBox("Don't ask again", &box);
    box.setCheckBox(dontAsk);
    box.exec();

    if (dontAsk->isChecked())
        solero::AppConfig::instance().setPromptAfterBrowserDownload(false);

    if (box.clickedButton() == viewBtn) {
        // Leave the browser and reveal the download (same path as the toggle).
        if (m_browseAction && m_browseAction->isChecked()) m_browseAction->setChecked(false);
        m_rightPane->showDownloadsTab();
    }
    // "Keep Browsing" -> stay in the browser; the download continues in the background.

    if (dontAsk->isChecked())
        solero::AppConfig::instance().save();   // persist the new preference
}

void MainWindow::onToggleNexus(bool on) {
    m_centralStack->setCurrentWidget(on ? static_cast<QWidget*>(m_nexusWeb)
                                        : m_modManagerPage);
    if (m_browseAction)
        m_browseAction->setText(on ? "\xe2\x86\x90 Mod Manager" : "\xe2\x86\x92 Browse Nexus");
}

void MainWindow::onNexusDownload(const QString& modId, const QString& fileId,
                                 const QString& fileName, const QString& version) {
    QString url = solero::NexusApi::downloadUrl(modId, fileId);
    if (url.isEmpty()) {
        QMessageBox::information(this, "Download",
            "In-app download needs Nexus Premium. Use the mod page's "
            "'Mod Manager Download' (nxm) button on the website instead.");
        return;
    }
    // Record metadata so the sidecar + update-checker work, exactly like the nxm path.
    m_nxmMeta[fileName] = QJsonObject{
        {"game", "skyrimspecialedition"}, {"modId", modId},
        {"fileId", fileId}, {"version", version}};
    m_downloads->enqueue(url, fileName, solero::AppConfig::instance().downloadsDir());
    m_rightPane->showDownloadsTab();
    statusBar()->showMessage("Downloading " + fileName + "\xe2\x80\xa6");
}

QString MainWindow::profilesRoot() const {
    return solero::AppConfig::dataRoot() + "/profiles";
}

QString MainWindow::txLogPath() const {
    return solero::AppConfig::dataRoot() + "/ai-transactions.json";
}

void MainWindow::setupMenuBar() {
    const QChar ell(0x2026);
    auto* mb = menuBar();

    // ---- File: acquire mods + move profiles between machines + app-level ----
    QMenu* fileMenu = mb->addMenu("&File");
    QAction* installAct = fileMenu->addAction(
        QStringLiteral("Install Mod") + ell, this, &MainWindow::onInstallMod);
    installAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    fileMenu->addAction(QStringLiteral("Install Wabbajack Modlist") + ell, this, &MainWindow::onInstallWabbajack);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Export Profile") + ell, this, &MainWindow::onExportProfile);
    fileMenu->addAction(QStringLiteral("Import Profile") + ell, this, &MainWindow::onImportProfile);
    fileMenu->addSeparator();
    QAction* settingsAct = fileMenu->addAction(
        QStringLiteral("Settings") + ell, this, [this]{ openSettingsDialog(); });
    settingsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    fileMenu->addSeparator();
    fileMenu->addAction("Quit", QKeySequence(QKeySequence::Quit), this, &QWidget::close);

    // ---- Profile: create/rename/delete + imports + update/FOMOD scans ----
    QMenu* profileMenu = mb->addMenu("&Profile");
    profileMenu->setToolTipsVisible(true); // custom action tooltips (e.g. Check for Updates)
    profileMenu->addAction(QStringLiteral("New Profile") + ell, this, &MainWindow::onNewProfile);
    profileMenu->addAction(QStringLiteral("Rename Current Profile") + ell, this, &MainWindow::onRenameProfile);
    profileMenu->addSeparator();
    profileMenu->addAction(QStringLiteral("Import MO2 Profile") + ell, this, &MainWindow::onImportMo2);
    profileMenu->addSeparator();
    // Same QAction as before: keeps the F5 shortcut + disable-while-running state.
    m_checkUpdatesAction = profileMenu->addAction(
        "Check for Updates", this, &MainWindow::onCheckUpdates);
    m_checkUpdatesAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_checkUpdatesAction->setToolTip("Check installed mods for newer files on Nexus (F5)");
    profileMenu->addSeparator();
    // Destructive - kept last with an ellipsis (opens a confirm dialog).
    profileMenu->addAction(QStringLiteral("Delete Current Profile") + ell, this, &MainWindow::onDeleteProfile);

    // ---- Tools: DYNAMIC - repopulated from activeTools() each time it opens,
    // mirroring the old dropdown (which rebuilt via rebuildToolsMenu()). ----
    m_toolsMenu = mb->addMenu("&Tools");
    connect(m_toolsMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildToolsMenu);

    // ---- View: the Browse Nexus page toggle (same checkable QAction) ----
    QMenu* viewMenu = mb->addMenu("&View");
    viewMenu->setToolTipsVisible(true); // custom action tooltips (e.g. Browse Nexus)
    m_browseAction = viewMenu->addAction("Browse Nexus");
    m_browseAction->setCheckable(true);
    m_browseAction->setToolTip("Toggle a full nexusmods.com browser in the main view");
    connect(m_browseAction, &QAction::toggled, this, &MainWindow::onToggleNexus);

    // ---- View ▸ Zoom: presets + in/out/reset, all on the one zoom mechanism ----
    viewMenu->addSeparator();
    QMenu* zoomMenu = viewMenu->addMenu("&Zoom");
    // Disabled label reads the live factor; useful when it's between presets.
    m_zoomCurrentAction = zoomMenu->addAction("Current: 100%");
    m_zoomCurrentAction->setEnabled(false);
    zoomMenu->addSeparator();
    m_zoomGroup = new QActionGroup(this);
    m_zoomGroup->setExclusive(true);
    const QList<int> presets{75, 90, 100, 110, 125, 150};
    for (int pct : presets) {
        QAction* a = zoomMenu->addAction(QString("%1%").arg(pct));
        a->setCheckable(true);
        a->setData(pct);
        m_zoomGroup->addAction(a);
        connect(a, &QAction::triggered, this, [pct] {
            static_cast<Application*>(qApp)->setZoomFactor(pct / 100.0);
        });
    }
    zoomMenu->addSeparator();
    QAction* zoomInAct = zoomMenu->addAction("Zoom In", this, &MainWindow::onZoomIn);
    zoomInAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Plus),
                             QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    QAction* zoomOutAct = zoomMenu->addAction("Zoom Out", this, &MainWindow::onZoomOut);
    zoomOutAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    QAction* zoomResetAct = zoomMenu->addAction("Reset Zoom", this, &MainWindow::onZoomReset);
    zoomResetAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    // Refresh checks + label each time the submenu opens.
    connect(zoomMenu, &QMenu::aboutToShow, this, &MainWindow::updateZoomMenu);
    updateZoomMenu();

    // ---- Help: keyboard reference + about ----
    QMenu* helpMenu = mb->addMenu("&Help");
    QAction* shortcutsAct = helpMenu->addAction(
        QStringLiteral("Keyboard Shortcuts") + ell, this, &MainWindow::onShowShortcuts);
    shortcutsAct->setShortcut(QKeySequence(Qt::Key_F1));
    // Keyboard/menu-reachable entry to the Problems dialog (otherwise only the
    // toolbar health icon opens it).
    helpMenu->addAction(QStringLiteral("Problems") + ell, this, &MainWindow::onShowProblems);
    helpMenu->addSeparator();
    helpMenu->addAction(QStringLiteral("Report Issue") + ell, this, &MainWindow::onReportIssue);
    // Quick access to the log folder so a user can grab solero.log for a bug report
    // even without the report relay configured.
    helpMenu->addAction(QStringLiteral("Open Log Folder") + ell, this, [] {
        const QString dir = QFileInfo(solero::logFilePath()).absolutePath();
        if (!dir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    helpMenu->addAction("About Solero", this, &MainWindow::onAboutSolero);

    rebuildToolsMenu();
}

void MainWindow::onShowShortcuts() {
    solero::KeyboardShortcutsDialog dlg(this, this);
    dlg.exec();
}

void MainWindow::onReportIssue() {
    int modCount = -1;
    if (m_profileMgr) if (auto* p = m_profileMgr->activeProfile())
        modCount = p->modList().count();
    solero::ReportDialog dlg(solero::ReportDialog::Mode::Issue, this);
    dlg.setContext(modCount, /*logOffset=*/0);
    dlg.exec();
}

void MainWindow::maybePromptCrashReport() {
    if (!solero::lastRunCrashed()) return;
    int modCount = -1;
    if (m_profileMgr) if (auto* p = m_profileMgr->activeProfile())
        modCount = p->modList().count();
    solero::ReportDialog dlg(solero::ReportDialog::Mode::Crash, this);
    dlg.setContext(modCount, solero::crashLogOffset());
    dlg.exec();
}

void MainWindow::onAboutSolero() {
    QMessageBox::about(this, "About Solero",
        QStringLiteral("<b>Solero</b> %1<br><br>An MO2-style mod manager for "
                       "Skyrim Special / Anniversary Edition.")
            .arg(qApp->applicationVersion()));
}

void MainWindow::updateZoomMenu() {
    if (!m_zoomGroup) return;
    const double factor = static_cast<Application*>(qApp)->zoomFactor();
    const int cur = qRound(factor * 100.0);
    if (m_zoomCurrentAction)
        m_zoomCurrentAction->setText(QString("Current: %1%").arg(cur));
    // Check the matching preset; leave all unchecked when between presets.
    for (QAction* a : m_zoomGroup->actions())
        a->setChecked(a->data().toInt() == cur);
}

void MainWindow::setupToolbar() {
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setIconSize({24, 24});

    // Profile selector (all other actions now live in the menu bar).
    tb->addWidget(new QLabel("Profile: "));
    m_profileCombo = new QComboBox(tb);
    m_profileCombo->setMinimumWidth(150);
    refreshProfileCombo();
    connect(m_profileCombo, &QComboBox::currentTextChanged,
            this, &MainWindow::switchProfile);
    tb->addWidget(m_profileCombo);

    // Stretch pushes the primary Deploy/Play pair to the right edge.
    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    // Problems / health indicator: a severity-coloured exclamation sitting just
    // LEFT of the Deploy/Play pair. Grey = clean, amber = warnings, red = errors.
    // Click opens the ProblemsDialog. refreshHealthIndicator() drives icon+tooltip.
    m_problemsBtn = new QToolButton(tb);
    m_problemsBtn->setAutoRaise(true);
    m_problemsBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_problemsBtn->setIcon(solero::neutralSignIcon(20));
    m_problemsBtn->setAccessibleName("Problems");
    m_problemsBtn->setToolTip("No problems");
    connect(m_problemsBtn, &QToolButton::clicked, this, &MainWindow::onShowProblems);
    tb->addWidget(m_problemsBtn);

    // Deploy + Play are THE primary controls - the workflow's terminus.
    m_deployAction = tb->addAction("\xe2\x9c\x97 Not Deployed", this, &MainWindow::onDeployToggle);
    m_deployAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_deployAction->setToolTip("Click to deploy mods to game directory (Ctrl+D)");
    m_playAction = tb->addAction("\xe2\x96\xb6 Play", this, &MainWindow::onPlay);
    m_playAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    m_playAction->setToolTip("Launch the game via Steam (Ctrl+P)");

    // Promote the pair so it dominates: bold both, and give Play a filled
    // accent pill pulled from the resolved palette (Highlight role) - no
    // hardcoded colours. The Deploy label mutates at runtime; the buttons
    // keep this styling across setText().
    if (auto* deployBtn = qobject_cast<QToolButton*>(tb->widgetForAction(m_deployAction))) {
        QFont f = deployBtn->font(); f.setBold(true); deployBtn->setFont(f);
    }
    if (auto* playBtn = qobject_cast<QToolButton*>(tb->widgetForAction(m_playAction))) {
        QFont f = playBtn->font(); f.setBold(true); playBtn->setFont(f);
        const QColor accent = palette().color(QPalette::Highlight);
        const QColor accentText = palette().color(QPalette::HighlightedText);
        // Disabled = greyed pill (shown while the game is launching/running).
        const QColor off = palette().color(QPalette::Disabled, QPalette::Button);
        const QColor offText = palette().color(QPalette::Disabled, QPalette::ButtonText);
        playBtn->setStyleSheet(QString(
            "QToolButton { background:%1; color:%2; border-radius:3px;"
            " padding:2px 10px; }"
            "QToolButton:disabled { background:%3; color:%4; }")
            .arg(accent.name(), accentText.name(), off.name(), offText.name()));
    }
}

void MainWindow::setupStatusBar() {
    // The Problems/health indicator now lives in the toolbar (see setupToolbar),
    // next to Deploy/Play. Touch the status bar so it exists for transient
    // messages (checkSave() warnings, etc.) but keep it otherwise empty.
    statusBar();
}

void MainWindow::setupCentralWidget() {
    auto* outer = new QSplitter(Qt::Vertical, this);
    m_splitter = new QSplitter(Qt::Horizontal, outer);

    // Left pane: a filter row (name box + state quick-filter) above the mod list.
    auto* leftContainer = new QWidget(m_splitter);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);
    auto* filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->setSpacing(4);
    auto* modFilter = new QLineEdit(leftContainer);
    m_modFilter = modFilter; // targeted by the Ctrl+F focus shortcut
    modFilter->setPlaceholderText(QStringLiteral("Search mods") + QChar(0x2026));
    modFilter->setClearButtonEnabled(true);
    auto* stateFilter = new QComboBox(leftContainer);
    stateFilter->setToolTip("Show only mods in a given state");
    stateFilter->addItem("All",              int(solero::ModListView::StateFilter::All));
    stateFilter->addItem("Has conflicts",    int(solero::ModListView::StateFilter::Conflicts));
    stateFilter->addItem("Update available", int(solero::ModListView::StateFilter::UpdateAvailable));
    stateFilter->addItem("Enabled",          int(solero::ModListView::StateFilter::Enabled));
    stateFilter->addItem("Disabled",         int(solero::ModListView::StateFilter::Disabled));
    stateFilter->addItem("Missing dependency", int(solero::ModListView::StateFilter::MissingDep));

    // facets: filter by category (separator section) and by a per-mod flag,
    // combined with the name + state filters above.
    auto* categoryFilter = new QComboBox(leftContainer);
    categoryFilter->setToolTip("Show only mods under a given separator");
    m_categoryCombo = categoryFilter; // repopulated per-profile by refreshCategoryFilter()

    auto* flagFilter = new QComboBox(leftContainer);
    flagFilter->setToolTip("Show only mods with a given flag");
    flagFilter->addItem("Any flag",      int(solero::ModListView::FlagFilter::All));
    flagFilter->addItem("FOMOD",         int(solero::ModListView::FlagFilter::Fomod));
    flagFilter->addItem("Output mod",    int(solero::ModListView::FlagFilter::Output));
    flagFilter->addItem("Has note",      int(solero::ModListView::FlagFilter::HasNote));

    filterRow->addWidget(modFilter, 1);
    filterRow->addWidget(stateFilter);
    filterRow->addWidget(categoryFilter);
    filterRow->addWidget(flagFilter);

    // Reorder Undo/Redo for the mod list. Curvy-arrow theme icons with a glyph
    // fallback (QChar, never a byte escape) when the icon theme lacks them.
    auto makeReorderBtn = [&](const QString& themeIcon, QChar fallback, const QString& tip) {
        auto* b = new QToolButton(leftContainer);
        const QIcon ic = QIcon::fromTheme(themeIcon);
        if (ic.isNull()) b->setText(QString(fallback));
        else             b->setIcon(ic);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setAutoRaise(true);
        b->setEnabled(false); // nothing to undo/redo until a reorder happens
        return b;
    };
    auto* undoBtn = makeReorderBtn(QStringLiteral("edit-undo"), QChar(0x21B6), tr("Undo move (Ctrl+Z)"));
    auto* redoBtn = makeReorderBtn(QStringLiteral("edit-redo"), QChar(0x21B7), tr("Redo move (Ctrl+Shift+Z)"));
    m_undoBtn = undoBtn; // mirrored by Ctrl+Z
    m_redoBtn = redoBtn; // mirrored by Ctrl+Shift+Z
    filterRow->addWidget(undoBtn);
    filterRow->addWidget(redoBtn);

    m_modListView = new solero::ModListView(leftContainer);
    connect(modFilter, &QLineEdit::textChanged, m_modListView,
            &solero::ModListView::setFilter);
    connect(stateFilter, &QComboBox::currentIndexChanged, this, [this, stateFilter](int){
        m_modListView->setStateFilter(
            static_cast<solero::ModListView::StateFilter>(stateFilter->currentData().toInt()));
    });
    connect(categoryFilter, &QComboBox::currentIndexChanged, this, [this, categoryFilter](int){
        m_modListView->setCategoryFilter(categoryFilter->currentData().toString());
    });
    connect(flagFilter, &QComboBox::currentIndexChanged, this, [this, flagFilter](int){
        m_modListView->setFlagFilter(
            static_cast<solero::ModListView::FlagFilter>(flagFilter->currentData().toInt()));
    });
    // The category facet depends on the profile's separators, which change per
    // profile and on mod-list edits; keep the combo's items current.
    connect(m_modListView, &solero::ModListView::modsChanged,   this, &MainWindow::refreshCategoryFilter);
    connect(m_modListView, &solero::ModListView::modsReordered, this, &MainWindow::refreshCategoryFilter);
    // Jump-to-mod (Problems panel / plugin origin) may need to drop an active filter
    // to reveal its target. Reset the filter WIDGETS here so the box and the list
    // stay in sync - their own signals drive setFilter("")/setStateFilter(All)/etc.
    connect(m_modListView, &solero::ModListView::filterCleared, this,
            [modFilter, stateFilter, categoryFilter, flagFilter]{
                modFilter->clear();              // -> setFilter("")
                stateFilter->setCurrentIndex(0); // "All" -> setStateFilter(All)
                categoryFilter->setCurrentIndex(0); // -> setCategoryFilter("")
                flagFilter->setCurrentIndex(0);     // -> setFlagFilter(All)
            });
    connect(undoBtn, &QToolButton::clicked, m_modListView, &solero::ModListView::undoMove);
    connect(redoBtn, &QToolButton::clicked, m_modListView, &solero::ModListView::redoMove);
    connect(m_modListView, &solero::ModListView::undoRedoStateChanged, this,
            [undoBtn, redoBtn](bool canUndo, bool canRedo){
                undoBtn->setEnabled(canUndo);
                redoBtn->setEnabled(canRedo);
            });
    leftLayout->addLayout(filterRow);
    leftLayout->addWidget(m_modListView);

    m_rightPane   = new solero::RightPane(m_splitter);
    m_bethiniWindow = new solero::BethiniWindow(this); // shown as a top-level modal window
    m_bethiniWindow->setWindowFlag(Qt::Window, true);  // not an in-canvas child of MainWindow
    m_bethiniWindow->hide();                           // stays hidden until the BethINI button

    m_splitter->addWidget(leftContainer);
    m_splitter->addWidget(m_rightPane);
    m_splitter->setSizes({640, 640});

    m_downloads = new solero::DownloadManager(this);
    connect(m_downloads, &solero::DownloadManager::progress, this,
        [this](const QString& fn, qint64 r, qint64 t){
            m_rightPane->downloadsTab()->setDownloadProgress(fn, r, t);
        });
    connect(m_downloads, &solero::DownloadManager::finished, this,
        [this](const QString& fn, const QString& path, bool ok, const QString& err){
            // "Update Mod" downloads reinstall the EXISTING mod in place instead of
            // adding a new one. Handle (and consume) those before the normal path.
            if (m_pendingUpdates.contains(fn)) {
                const PendingUpdate pu = m_pendingUpdates.take(fn);
                m_rightPane->downloadsTab()->setDownloadProgress(fn, ok ? 1 : 0, ok ? 1 : 0);
                m_rightPane->downloadsTab()->refresh();
                if (!ok || path.isEmpty()) {
                    statusBar()->showMessage("Update download failed: " + fn);
                    QMessageBox::warning(this, "Update Download Failed",
                        "Couldn't download the update for " + fn + ".\n\n" + err);
                    return;
                }
                auto* profile = m_profileMgr->activeProfile();
                solero::ModEntry* existing =
                    profile ? profile->modList().findById(pu.modId) : nullptr;
                if (existing) {
                    existing->sourceArchive = path;
                    existing->version       = pu.version;
                    existing->nexusFileId   = pu.fileId;
                    // Re-clean the name in case it still carries the Nexus
                    // id/version/timestamp tail (e.g. installed before the
                    // name-cleaning fix). No-op for already-clean/custom names.
                    existing->name = cleanModName(existing->name, existing->nexusModId);
                    profile->save();
                    onReinstallMod(pu.modId);
                    // onReinstallMod preserves version/nexusFileId (it only touches
                    // hasFomodChoices + sourceArchive), but re-assert the version in
                    // case a re-prompt path changed it, then persist.
                    if (auto* e2 = profile->modList().findById(pu.modId)) {
                        if (e2->version != pu.version) {
                            e2->version = pu.version;
                            profile->save();
                        }
                        statusBar()->showMessage("Updated " + e2->name + " to " + pu.version + ".");
                    }
                    // This mod is now current - drop just its update flag and persist,
                    // leaving every other mod's flag intact (setProfile during the
                    // reinstall above no longer wipes them).
                    {
                        auto flags = loadUpdateFlags();
                        if (flags.remove(pu.modId) > 0) {
                            saveUpdateFlags(flags);
                            m_modListView->setUpdateInfo(flags);
                        }
                    }
                    return; // do not fall through to the install-as-new path
                }
                // Mod no longer exists - fall through and treat as a fresh install.
            }
            // For an nxm-originated download, persist the captured Nexus metadata as a
            // sidecar next to the archive so installFromArchive can tag the mod.
            if (ok && m_nxmMeta.contains(fn) && !path.isEmpty()) {
                QFile sf(path + ".solero-nexus.json");
                if (sf.open(QIODevice::WriteOnly))
                    sf.write(QJsonDocument(m_nxmMeta.value(fn)).toJson(QJsonDocument::Indented));
            }

            // Auto-download-on-reinstall: the archive was missing, so onReinstallMod
            // enqueued it. The sidecar is now on disk, so re-enter reinstall - its
            // findDownloadArchivesFor() locates this file by (modId, fileId) and the
            // install proceeds. Consumed before the normal install-as-new path.
            if (m_pendingReinstalls.contains(fn)) {
                const QString reModId = m_pendingReinstalls.take(fn);
                m_rightPane->downloadsTab()->setDownloadProgress(fn, ok ? 1 : 0, ok ? 1 : 0);
                m_rightPane->downloadsTab()->refresh();
                m_nxmMeta.remove(fn);
                if (!ok || path.isEmpty()) {
                    statusBar()->showMessage("Reinstall download failed: " + fn);
                    QMessageBox::warning(this, "Reinstall Download Failed",
                        "Couldn't download the archive for " + fn + ".\n\n" + err);
                    return;
                }
                onReinstallMod(reModId);
                return; // do not fall through to the install-as-new path
            }

            // Drop any older failed entry for this filename (about to be re-evaluated).
            for (int i = m_failedDownloads.size() - 1; i >= 0; --i)
                if (m_failedDownloads[i].fileName == fn) m_failedDownloads.removeAt(i);

            if (!ok && err != "cancelled") {
                // Retain context so the Downloads tab can offer a retry. Retry
                // re-resolves the URL (CDN links expire), so keep the Nexus meta.
                FailedDownload fd;
                fd.fileName = fn;
                fd.meta = m_nxmMeta.value(fn);  // empty for non-Nexus downloads
                fd.error = err;
                m_failedDownloads.append(fd);
            }
            m_nxmMeta.remove(fn);

            m_rightPane->downloadsTab()->setDownloadProgress(fn, ok ? 1 : 0, ok ? 1 : 0); // mark complete

            QList<QPair<QString,QString>> failPairs;
            for (const FailedDownload& d : m_failedDownloads)
                failPairs.append({d.fileName, d.error});
            m_rightPane->downloadsTab()->setFailedDownloads(failPairs); // calls refresh() once
            statusBar()->showMessage(ok ? ("Downloaded: " + fn) : ("Download failed: " + fn));
            if (!ok && err != "cancelled")
                QMessageBox::warning(this, "Download Failed",
                    "Couldn't download " + fn + ".\n\n" + err);
            // Auto-install flagged downloads (e.g. SKSE) once they finish. Runs
            // after the sidecar write above so the new mod gets tagged with its
            // Nexus identity. The update branch returns early, so no collision.
            if (m_autoInstall.contains(fn)) {
                m_autoInstall.remove(fn);
                m_skseInstalling = false;
                if (ok && !path.isEmpty()) installFromArchive(path);
            }
        });

    connect(m_rightPane->downloadsTab(), &solero::DownloadsTab::installRequested,
            this, &MainWindow::installFromArchive);
    connect(m_rightPane->downloadsTab(), &solero::DownloadsTab::cancelRequested,
            this, [this](const QString& fn){ m_downloads->cancel(fn); });
    connect(m_rightPane->downloadsTab(), &solero::DownloadsTab::retryRequested,
            this, &MainWindow::onRetryDownload);
    connect(m_modListView, &solero::ModListView::modsSelected,
            m_rightPane, &solero::RightPane::onSelectionChanged);
    connect(m_modListView, &solero::ModListView::reinstallRequested,
            this, &MainWindow::onReinstallMod);
    connect(m_modListView, &solero::ModListView::redownloadRequested,
            this, &MainWindow::onRedownloadMod);
    connect(m_modListView, &solero::ModListView::endorseRequested,
            this, &MainWindow::onEndorseMod);
    connect(m_modListView, &solero::ModListView::trackRequested,
            this, &MainWindow::onTrackMod);
    connect(m_modListView, &solero::ModListView::untrackRequested,
            this, &MainWindow::onUntrackMod);
    connect(m_modListView, &solero::ModListView::viewNexusPageRequested,
            this, &MainWindow::onViewNexusPage);
    connect(m_modListView, &solero::ModListView::updateRequested,
            this, &MainWindow::onUpdateMod);
    connect(m_modListView, &solero::ModListView::modsChanged,
            this, &MainWindow::onModsChanged);
    connect(m_modListView, &solero::ModListView::modsReordered,
            this, &MainWindow::onModsReordered);
    connect(m_modListView, &solero::ModListView::modActivated,
            m_rightPane, &solero::RightPane::showDataFor);
    connect(m_modListView, &solero::ModListView::variantSwitched,
            this, &MainWindow::onVariantSwitched);
    connect(m_modListView, &solero::ModListView::createModFromOverwriteRequested,
            this, &MainWindow::onCreateModFromOverwrite);
    connect(m_modListView, &solero::ModListView::clearShaderCacheRequested,
            this, &MainWindow::onClearShaderCache);
    connect(m_modListView, &solero::ModListView::saveFailed,
            this, &MainWindow::reportSaveFailure);

    // Plugins tab: manual reorder marks the load order dirty; "Sort Now" runs LOOT.
    connect(m_rightPane->pluginsView(), &solero::PluginListView::loadOrderChanged,
            this, &MainWindow::onLoadOrderChanged);
    // Enable/disable a plugin -> recompute the health indicator / Problems panel
    // live (missing-master warnings appear/clear immediately).
    connect(m_rightPane->pluginsView(), &solero::PluginListView::pluginEnabledChanged,
            this, &MainWindow::refreshHealthIndicator);
    connect(m_rightPane, &solero::RightPane::sortRequested,
            this, &MainWindow::onSortRequested);
    connect(m_rightPane, &solero::RightPane::lockOrderToggled,
            this, &MainWindow::onLockOrderToggled);
    connect(m_rightPane, &solero::RightPane::lootRulesRequested,
            this, &MainWindow::onOpenLootRules);
    connect(m_rightPane, &solero::RightPane::backupLoRequested,
            this, &MainWindow::onBackupLo);
    connect(m_rightPane, &solero::RightPane::restoreLoRequested,
            this, &MainWindow::onRestoreLo);
    // A per-file rule changed (hide a file in a mod / force a per-path winner):
    // the staged outputs no longer match what's deployed, so mark dirty.
    connect(m_rightPane, &solero::RightPane::fileRulesChanged, this, [this]{
        if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
        updatePluginNotice();
    });
    // Rename / delete of a staged file or folder from the Data tab: perform the
    // filesystem op on the mod's staging dir, then refresh + mark dirty.
    connect(m_rightPane, &solero::RightPane::renameRequested,
            this, &MainWindow::onDataRename);
    connect(m_rightPane, &solero::RightPane::deleteRequested,
            this, &MainWindow::onDataDelete);
    connect(m_rightPane, &solero::RightPane::highlightOriginMods, this,
            [this](const QHash<QString,int>& roles) {
        m_modListView->setPluginOriginHighlights(roles);
    });
    connect(m_rightPane, &solero::RightPane::goToOriginMod, this,
            [this](const QString& id) {
        if (m_browseAction && m_browseAction->isChecked())
            m_browseAction->setChecked(false); // leave the Nexus browser first
        m_modListView->selectModById(id);
    });

    m_bottomPanel = new solero::BottomPanel(outer);
    connect(m_modListView, &solero::ModListView::modsSelected,
            m_bottomPanel, &solero::BottomPanel::onModsSelected);
    // A note edited in the Mod Info panel refreshes the list's note indicator.
    connect(m_bottomPanel, &solero::BottomPanel::noteChanged,
            m_modListView, &solero::ModListView::refreshFlags);
    outer->addWidget(m_splitter);
    outer->addWidget(m_bottomPanel);
    outer->setSizes({580, 200});

    // The central area is a stack: page 0 = the mod-manager view, page 1 =
    // the embedded Nexus web browser, toggled from the toolbar.
    m_modManagerPage = outer;
    m_centralStack = new QStackedWidget(this);
    m_centralStack->addWidget(outer);
    setCentralWidget(m_centralStack);

    // Construct the Nexus web view EAGERLY (before the window is shown). The first
    // QWebEngineView forces the top-level native handle to be recreated; doing it
    // here during startup avoids the window appearing to close+reopen on first use.
    m_nexusWeb = new solero::NexusWebView(this);
    m_centralStack->addWidget(m_nexusWeb);
    connect(m_nexusWeb, &solero::NexusWebView::nxmRequested,
            this, &MainWindow::onBrowserNxmDownload);

    // Window-wide shortcuts for controls that aren't menu/toolbar actions
    // (Menu/toolbar actions carry their own shortcuts; these cover the search
    //  boxes and the mod-move undo/redo buttons.) NOTE: keep this list in sync
    //  with the static entries in KeyboardShortcutsDialog and the ModListView
    //  Del/Space handlers.
    // Ctrl+F -> focus the mod filter box.
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this), &QShortcut::activated,
            this, [this]{ if (m_modFilter) { m_modFilter->setFocus(Qt::ShortcutFocusReason); m_modFilter->selectAll(); } });
    // Ctrl+Shift+F -> focus the plugin search box (switches to the Plugins tab).
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), this), &QShortcut::activated,
            this, [this]{ if (m_rightPane) m_rightPane->focusPluginSearch(); });
    // Ctrl+Z / Ctrl+Shift+Z -> mod-move undo/redo, only while the button is enabled.
    // Guard: if a text widget has focus, let it handle its own undo so we don't
    // swallow the keystroke while the user is editing a search box or note field.
    auto textWidgetFocused = [] {
        QWidget* w = qApp->focusWidget();
        return w && (qobject_cast<QLineEdit*>(w)
                     || qobject_cast<QTextEdit*>(w)
                     || qobject_cast<QPlainTextEdit*>(w));
    };
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this), &QShortcut::activated,
            this, [this, textWidgetFocused]{
                if (!textWidgetFocused() && m_undoBtn && m_undoBtn->isEnabled())
                    m_undoBtn->click();
            });
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this), &QShortcut::activated,
            this, [this, textWidgetFocused]{
                if (!textWidgetFocused() && m_redoBtn && m_redoBtn->isEnabled())
                    m_redoBtn->click();
            });
}

// Find a file in dir case-insensitively (Skyrim's custom ini ships as
// "Skyrimcustom.ini" in some installs). Returns the actual path or empty.
static QString findIniCaseInsensitive(const QString& dir, const QString& wanted) {
    QDir d(dir);
    for (const auto& e : d.entryList(QDir::Files))
        if (e.compare(wanted, Qt::CaseInsensitive) == 0)
            return d.absoluteFilePath(e);
    return {};
}

// Seed a profile's INI copies from the live game INIs (in the Proton documents
// dir) the first time, so the BethINI editor starts from the real baseline.
static void seedProfileInis(solero::Profile* profile) {
    QString docs = solero::AppConfig::instance().documentsDir();
    if (docs.isEmpty()) return;
    const QList<QPair<QString,QString>> map = {
        {"Skyrim.ini",       profile->skyrimIniPath()},
        {"SkyrimPrefs.ini",  profile->skyrimPrefsPath()},
        {"SkyrimCustom.ini", profile->skyrimCustomPath()},
    };
    for (const auto& [liveName, target] : map) {
        if (QFile::exists(target)) continue; // never clobber profile edits
        QString live = findIniCaseInsensitive(docs, liveName);
        if (!live.isEmpty()) {
            QDir().mkpath(QFileInfo(target).path());
            QFile::copy(live, target);
        }
    }
}

void MainWindow::switchProfile(const QString& name) {
    if (name.isEmpty()) return;
    // Re-entrancy guard: this method pumps the event loop (ProgressModal) during
    // deploy/undeploy, which can re-dispatch a combo change and re-enter here -
    // a nested loadProfile would free the profile this call is mid-switch on.
    if (m_switchingProfile) return;
    m_switchingProfile = true;
    struct Reset { bool& b; ~Reset() { b = false; } } reset{m_switchingProfile};
    if (m_toolRunning) {
        statusBar()->showMessage("A tool is running - please wait for it to finish.");
        // The combo may have driven this switch; revert its text to the active
        // profile so the displayed selection doesn't desync from reality.
        if (m_profileCombo) {
            if (auto* active = m_profileMgr->activeProfile()) {
                QSignalBlocker blocker(m_profileCombo);
                m_profileCombo->setCurrentText(active->name());
            }
        }
        return;
    }
    bool wasDeployed = m_deployed;
    std::unique_ptr<solero::ProgressModal> prog;
    if (wasDeployed && solero::AppConfig::instance().isConfigured()) {
        prog = std::make_unique<solero::ProgressModal>(this, "Switch Profile", "Undeploying current...");
        prog->show(); prog->pump();
    }
    // Undeploy the outgoing profile's files first (deploy record is in the game dir).
    if (wasDeployed && solero::AppConfig::instance().isConfigured()) {
        solero::DeployEngine engine(solero::AppConfig::instance().gameDir(),
                                    solero::AppConfig::instance().stagingDir());
        if (!engine.undeploy(solero::AppConfig::instance().gameDir(),
                        [&](int d, int t){ if (prog) { prog->setProgress(d, t); prog->pump(); } }))
            statusBar()->showMessage(
                "Undeploy of the previous profile finished with warnings - some "
                "deployed files may remain in the game folder.", 6000);
    }

    if (prog) prog->setMessage("Loading " + name + "...");
    auto* profile = m_profileMgr->loadProfile(name);
    seedProfileInis(profile);
    m_ipcServer->setActiveProfile(profile);
    // Tools are per-profile. We do not seed on switch: a profile's tools come from
    // its own executables.json (empty for new/other profiles). The legacy global
    // template is folded into the active profile exactly once, at startup, via the
    // app-level toolsMigratedToPerProfile flag (see the constructor). Just rebuild
    // the menu so it reflects this profile's activeTools().
    rebuildToolsMenu();
    m_modListView->setProfile(profile);
    refreshCategoryFilter(); // repopulate the category facet for this profile's separators
    // If we're about to redeploy (profile was deployed), defer the expensive
    // game-data plugin reconcile+save to the post-deploy setProfile below so it
    // runs exactly once. Otherwise reconcile now.
    const bool willRedeploy = wasDeployed && solero::AppConfig::instance().isConfigured();
    m_rightPane->setProfile(profile, /*reconcilePlugins=*/!willRedeploy);
    m_bottomPanel->setProfile(profile);
    // Invalidate any in-flight update check: its results belong to the OLD profile.
    // The finished handler sees a stale generation and discards them; the inline
    // progress readout stops immediately.
    ++m_updateGen;
    m_bottomPanel->setUpdateProgress(-1, 0);
    m_bethiniWindow->setProfile(profile);
    // Self-review fix: load previously-computed ConflictIndex if it exists
    QString conflictPath = solero::DeployEngine::conflictIndexPath(profile->path());
    if (QFile::exists(conflictPath)) {
        const auto ci = solero::ConflictIndex::loadFromFile(conflictPath);
        m_lastConflicts = ci;
        m_rightPane->setConflictIndex(ci);
        m_modListView->setConflictIndex(ci);
    } else {
        m_lastConflicts.clear();
    }
    // Show the last-known "update available" flags immediately (setProfile cleared
    // them); the auto-check at the end of this switch refreshes them.
    m_modListView->setUpdateInfo(loadUpdateFlags());
    m_lastDeployWarning.clear();  // stale once we switch profiles
    m_lastDeployHadFailures = false;
    setWindowTitle(QString("Solero - %1").arg(name));

    // Keep the toolbar selector in sync with the actually-active profile. The
    // startup restore (and onImportProfile) call switchProfile() directly rather
    // than through the combo, so without this the combo stays at index 0 - the
    // alphabetically-first profile - making every launch *look* like it opened
    // the first profile even though the correct one was loaded. Block signals so
    // this programmatic sync doesn't recursively re-enter switchProfile.
    if (m_profileCombo && m_profileCombo->currentText() != name) {
        QSignalBlocker blocker(m_profileCombo);
        m_profileCombo->setCurrentText(name);
    }

    // Deploy the incoming profile if the previous one was deployed.
    if (wasDeployed && solero::AppConfig::instance().isConfigured()) {
        if (prog) prog->setMessage("Deploying " + name + "...");
        statusBar()->showMessage("Switching profile - deploying " + name + "...");
        qApp->processEvents();
        solero::DeployEngine engine(solero::AppConfig::instance().gameDir(),
                                    solero::AppConfig::instance().stagingDir());
        engine.setUserlistPath(profile->lootUserlistPath());
        auto result = engine.deploy(*profile, solero::AppConfig::instance().deployMode(),
                                    [&](int d, int t){ if (prog) { prog->setProgress(d, t); prog->pump(); } });
        m_deployed = result.success;
        m_deployDirty = false;
        m_lastDeployWarning = result.warning;
        m_lastDeployHadFailures = !result.success;
        if (result.success) {
            m_lastConflicts = result.conflicts;
            m_rightPane->setConflictIndex(result.conflicts);
            m_modListView->setConflictIndex(result.conflicts);
            m_rightPane->setProfile(profile);
        }
        updateDeployButton();
        statusBar()->showMessage(QString("Switched to '%1' (redeployed %2 files).").arg(name).arg(result.filesDeployed));
    } else {
        statusBar()->showMessage(QString("Loaded profile: %1").arg(name));
    }
    // Persist the active profile so the next launch restores it instead of
    // defaulting to the first profile alphabetically.
    if (solero::AppConfig::instance().lastProfile() != name) {
        solero::AppConfig::instance().setLastProfile(name);
        solero::AppConfig::instance().save();
    }
    // A profile switch starts from a clean (just-loaded or just-deployed) order.
    m_loadOrderDirty = false;
    m_rightPane->setLockOrderChecked(profile->pluginList().loadOrderLocked());
    if (prog) prog->close();
    updatePluginNotice();
    updateSortButton();
    refreshHealthIndicator();

    // Auto-check Nexus for mod updates after a profile loads (throttled to once
    // every 6h, opt-out via Settings, no-op if no key / no Nexus-tagged mods).
    maybeAutoCheckUpdates();
}

void MainWindow::promptSwitchToNewProfile(const QString& newName,
                                          const QString& priorActiveName) {
    // First profile (nothing to stay on) - switch immediately, no prompt.
    if (!m_profileMgr->activeProfile()) {
        switchProfile(newName);
        return;
    }

    // The import path freed the previously-active Profile and made newName active,
    // so activeProfile()->name() == newName there; the manual path leaves the old
    // profile active and live. priorActiveName, when supplied, is the genuine
    // outgoing profile to fall back to on "No".
    const QString prior = priorActiveName.isEmpty()
                              ? m_profileMgr->activeProfile()->name()
                              : priorActiveName;

    QString text = QString("Profile '%1' created. Switch to it now?").arg(newName);
    if (m_deployed) {
        text += QString("\n\nYour current profile will be undeployed and '%1' "
                        "deployed in its place.").arg(newName);
    }
    auto ret = QMessageBox::question(this, "Switch Profile", text,
                                     QMessageBox::Yes | QMessageBox::No,
                                     QMessageBox::Yes);

    if (ret == QMessageBox::Yes) {
        switchProfile(newName);
        return;
    }

    // No: stay on the prior profile. If the manager already moved active to the
    // new profile (import path), the old Profile object was freed and its views
    // were detached, so reload the prior profile to re-attach views and keep the
    // deploy state consistent. Otherwise (manual path) the prior profile is still
    // active and attached - just restore the combo selection to it.
    if (m_profileMgr->activeProfile()->name() == newName) {
        switchProfile(prior);
    } else {
        refreshProfileCombo();
        if (m_profileCombo && m_profileCombo->currentText() != prior) {
            QSignalBlocker blocker(m_profileCombo);
            m_profileCombo->setCurrentText(prior);
        }
    }
}

void MainWindow::refreshProfileCombo() {
    QSignalBlocker blocker(m_profileCombo);
    m_profileCombo->clear();
    m_profileCombo->addItems(m_profileMgr->profileNames());
}

bool MainWindow::ensureDeployed(const QString& reason) {
    // Already deployed and clean -> nothing to do.
    if (m_deployed && !m_deployDirty) return true;

    // Honor the persisted "always deploy before launching" preference: skip the
    // modal and deploy silently.
    if (solero::AppConfig::instance().autoDeployBeforeLaunch()) {
        if (!deployCurrent()) {
            QMessageBox box(QMessageBox::Critical, "Deploy Failed",
                "Deployment failed. Fix the reported issue, then click Deploy again.",
                QMessageBox::Ok, this);
            if (!m_lastDeployError.isEmpty()) box.setDetailedText(m_lastDeployError);
            box.exec();
            return false;
        }
        return true;
    }

    QMessageBox box(this);
    box.setWindowTitle("Deploy required");
    box.setIcon(QMessageBox::Warning);
    box.setText(QString("Deploy your modlist before %1?").arg(reason));
    box.setInformativeText(QString("Mods only reach the game once deployed")
                           + (m_deployDirty ? " (you have undeployed changes)." : "."));
    QAbstractButton* deployBtn = box.addButton("Deploy", QMessageBox::AcceptRole);
    box.addButton("Cancel", QMessageBox::RejectRole);
    QCheckBox* always = new QCheckBox("Don't ask again - always deploy before launching", &box);
    box.setCheckBox(always);
    box.exec();
    if (box.clickedButton() != deployBtn) return false;
    if (always->isChecked()) {
        solero::AppConfig::instance().setAutoDeployBeforeLaunch(true);
        solero::AppConfig::instance().save();
    }
    if (!deployCurrent()) {
        QMessageBox box(QMessageBox::Critical, "Deploy Failed",
            "Deployment failed. Fix the reported issue, then click Deploy again.",
            QMessageBox::Ok, this);
        if (!m_lastDeployError.isEmpty()) box.setDetailedText(m_lastDeployError);
        box.exec();
        return false;
    }
    return true;
}

bool MainWindow::deployCurrent() {
    // Synchronous bool bridge over the off-thread deployActiveAsync(), for callers
    // whose surrounding flow is synchronous (ensureDeployed -> Play / tool runs). The
    // deploy itself still runs on a worker thread; a LOCAL event loop keeps the UI
    // responsive (repaints/progress) while we wait, rather than freezing it. The
    // m_deploying guard + app-modal ProgressModal + disabled controls prevent the
    // nested loop from re-entering a second deploy or a mutating action.
    bool ok = false, done = false;
    QEventLoop loop;
    deployActiveAsync([&](bool s){ ok = s; done = true; if (loop.isRunning()) loop.quit(); });
    if (!done) loop.exec(); // deployActiveAsync may fail its pre-checks synchronously
    return ok;
}

void MainWindow::deployActiveAsync(std::function<void(bool)> onDone) {
    // Hard re-entrancy guard ( / A16): never run a deploy against half-written
    // state left by another in-flight job.
    if (m_deploying) {
        statusBar()->showMessage("A deploy is already in progress\xe2\x80\xa6");
        if (onDone) onDone(false);
        return;
    }
    if (m_toolRunning) {
        statusBar()->showMessage("A tool is running - please wait for it to finish.");
        if (onDone) onDone(false);
        return;
    }
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile."); if (onDone) onDone(false); return; }

    // If Skyrim's AppData/Documents (inside the Proton prefix) couldn't be
    // located, deploy falls back to writing Plugins.txt into Data/, where
    // Skyrim SE will not read it - plugins silently won't load. Warn once.
    if (!m_warnedMissingAppData &&
        solero::AppConfig::instance().localAppDataDir().isEmpty()) {
        m_warnedMissingAppData = true;
        QMessageBox::warning(this, "Skyrim AppData Not Found",
            "Couldn't find Skyrim's save-data folder. Plugins.txt may not reach the game, "
            "so plugins may silently not load. Check the game path in Settings and make sure "
            "you've launched Skyrim at least once through Steam.");
    }

    statusBar()->showMessage("Deploying...");

    // Enter the job: set the guard, disable Deploy/Play, and suppress central-widget
    // repaints so no paint-time model read touches the Profile the worker mutates
    // (LOOT sort reorders the plugin list off-thread). The ProgressModal is
    // app-modal, so user input is blocked until the job finishes.
    m_deploying = true;
    m_savedDeployActionEnabled = m_deployAction && m_deployAction->isEnabled();
    m_savedPlayActionEnabled   = m_playAction   && m_playAction->isEnabled();
    if (m_deployAction) m_deployAction->setEnabled(false);
    if (m_playAction)   m_playAction->setEnabled(false);
    if (auto* cw = centralWidget()) cw->setUpdatesEnabled(false);

    m_deployModal = new solero::ProgressModal(this, "Deploy",
        "Deploying mods + sorting plugins with LOOT...");
    m_deployModal->show();
    m_deployCont = std::move(onDone);

    // Snapshot everything the worker needs so it never reaches back into the UI.
    const QString gameDir  = solero::AppConfig::instance().gameDir();
    const QString staging  = solero::AppConfig::instance().stagingDir();
    const auto    mode     = solero::AppConfig::instance().deployMode();
    const QString userlist = profile->lootUserlistPath();
    QPointer<solero::ProgressModal> modal(m_deployModal);

    qCInfo(lcDeploy) << "deploy dispatched to worker thread";
    auto future = QtConcurrent::run(
        [profile, gameDir, staging, mode, userlist, modal]() -> solero::DeployResult {
            solero::DeployEngine engine(gameDir, staging);
            engine.setUserlistPath(userlist);
            // Progress must not touch the widget from this thread - marshal each
            // tick to the UI thread via a queued call.
            return engine.deploy(*profile, mode, [modal](int d, int t){
                QMetaObject::invokeMethod(qApp, [modal, d, t]{
                    if (modal) modal->setProgress(d, t);
                }, Qt::QueuedConnection);
            });
        });
    m_deployWatcher.setFuture(future);
}

void MainWindow::undeployActiveAsync(std::function<void(bool)> onDone) {
    if (m_deploying) {
        statusBar()->showMessage("A deploy is already in progress\xe2\x80\xa6");
        if (onDone) onDone(false);
        return;
    }
    m_deploying = true;
    m_savedDeployActionEnabled = m_deployAction && m_deployAction->isEnabled();
    m_savedPlayActionEnabled   = m_playAction   && m_playAction->isEnabled();
    if (m_deployAction) m_deployAction->setEnabled(false);
    if (m_playAction)   m_playAction->setEnabled(false);
    if (auto* cw = centralWidget()) cw->setUpdatesEnabled(false);

    m_deployModal = new solero::ProgressModal(this, "Deploy", "Undeploying...");
    m_deployModal->show();
    m_undeployCont = std::move(onDone);

    const QString gameDir = solero::AppConfig::instance().gameDir();
    const QString staging = solero::AppConfig::instance().stagingDir();
    QPointer<solero::ProgressModal> modal(m_deployModal);

    qCInfo(lcDeploy) << "undeploy dispatched to worker thread";
    auto future = QtConcurrent::run([gameDir, staging, modal]() -> bool {
        solero::DeployEngine engine(gameDir, staging);
        return engine.undeploy(gameDir, [modal](int d, int t){
            QMetaObject::invokeMethod(qApp, [modal, d, t]{
                if (modal) modal->setProgress(d, t);
            }, Qt::QueuedConnection);
        });
    });
    m_undeployWatcher.setFuture(future);
}

void MainWindow::finishDeployJob() {
    if (m_deployModal) {
        m_deployModal->close();
        m_deployModal->deleteLater();
        m_deployModal = nullptr;
    }
    if (auto* cw = centralWidget()) cw->setUpdatesEnabled(true);
    if (m_deployAction) m_deployAction->setEnabled(m_savedDeployActionEnabled);
    if (m_playAction)   m_playAction->setEnabled(m_savedPlayActionEnabled);
    m_deploying = false;
    // Apply any update-check result that finished mid-deploy (deferred so it couldn't
    // race the worker's profile access). Only if its generation is still current.
    if (m_hasPendingUpdateScan) {
        m_hasPendingUpdateScan = false;
        if (m_pendingUpdateScan.gen == m_updateGen) applyUpdateScan(m_pendingUpdateScan);
        m_pendingUpdateScan = {};
    }
}

// Apply an update-check result on the UI thread (backfill fileIds/versions into the
// profile, persist, show the update flags). Never call while m_deploying - see the
// m_updateWatcher finished handler, which defers to here via finishDeployJob.
void MainWindow::applyUpdateScan(const UpdateScan& scan) {
    // Persist any fileIds resolved by MD5 this run so future checks are accurate
    // (and so Update/Redownload can act on them).
    if (!scan.backfill.isEmpty()) {
        if (auto* profile = m_profileMgr->activeProfile()) {
            for (auto it = scan.backfill.constBegin(); it != scan.backfill.constEnd(); ++it) {
                if (auto* e = profile->modList().findById(it.key())) {
                    e->nexusFileId = it.value().first;
                    if (!it.value().second.isEmpty()) e->version = it.value().second;
                }
            }
            profile->save();
        }
    }
    m_modListView->setUpdateInfo(scan.updates);
    saveUpdateFlags(scan.updates); // persist so the flags show at next launch
    solero::AppConfig::instance().setLastUpdateCheckEpoch(
        QDateTime::currentSecsSinceEpoch());
    solero::AppConfig::instance().save();
    if (scan.updates.isEmpty())
        statusBar()->showMessage("All mods up to date.");
    else
        statusBar()->showMessage(
            QString("%1 update(s) available.").arg(scan.updates.size()));
}

bool MainWindow::redeployForTool(const QString& title, const QString& message) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return false;

    solero::ProgressModal prog(this, title, message);
    prog.show(); prog.pump();

    solero::DeployEngine engine(
        solero::AppConfig::instance().gameDir(),
        solero::AppConfig::instance().stagingDir());
    engine.setUserlistPath(profile->lootUserlistPath());
    auto result = engine.deploy(*profile, solero::AppConfig::instance().deployMode(),
                                [&](int d, int t){ prog.setProgress(d, t); prog.pump(); });

    prog.close();

    if (result.success) {
        m_deployed = true;
        m_deployDirty = false;
        m_lastConflicts = result.conflicts;
        m_rightPane->setConflictIndex(result.conflicts);
        m_modListView->setConflictIndex(result.conflicts);
        emit conflictsUpdated(result.conflicts);
        updateDeployButton();
        refreshHealthIndicator();
    }
    return result.success;
}

void MainWindow::runPostDeployTools() {
    // Collect the flagged tools up front (in listed order) so the run loop doesn't
    // observe mid-run changes to the store.
    QList<solero::Executable> queue;
    for (const auto& t : activeTools())
        if (t.runThroughDeployer) queue.append(t);
    if (queue.isEmpty()) return;

    // m_toolRunning serializes runs and blocks re-entrant deploys (deployCurrent /
    // onDeployToggle / onRunTool all bail when it's set), so a post-deploy tool
    // can't trigger another deploy loop. A tool exiting normally never re-enters
    // here regardless.
    const QString gameDir = solero::AppConfig::instance().gameDir();
    const QString stagingDir = solero::AppConfig::instance().stagingDir();
    QStringList failed;
    for (const auto& exe : queue) {
        statusBar()->showMessage("Running post-deploy tool: " + exe.name);
        m_toolRunning = true;
        showRunLock(exe.name);
        QString outFolder, owDir;
        if (auto* p = m_profileMgr->activeProfile()) {
            if (!exe.outputModId.isEmpty()) outFolder = p->stagingFolderFor(exe.outputModId);
            owDir = solero::AppConfig::overwriteDir(p->name());
        }
        auto res = solero::ToolRunner::run(exe, gameDir, stagingDir, outFolder, owDir);
        hideRunLock();
        m_toolRunning = false;
        // Don't abort the rest of the queue if one tool fails - record it.
        if (!res.launched) failed.append(exe.name);
    }

    // A post-deploy tool may have captured output into a mod, changing staged
    // files - refresh exactly as onRunTool does after a run.
    m_modListView->invalidateModCache();
    m_rightPane->invalidateModPluginCache();
    if (auto* p = m_profileMgr->activeProfile()) { m_rightPane->refreshPlugins(p); m_modListView->setProfile(p); }
    updatePluginNotice();

    if (!failed.isEmpty())
        QMessageBox::warning(this, "Post-Deploy Tools",
            "These tools are set to run automatically after deployment, but failed to launch:\n\n"
            + failed.join("\n")
            + "\n\nCheck each tool's binary path in the Tools panel.");
    else
        statusBar()->showMessage("Post-deploy tools finished.");
}

void MainWindow::onDeployToggle() {
    if (m_deploying) {
        statusBar()->showMessage("A deploy is already in progress\xe2\x80\xa6");
        return;
    }
    if (m_toolRunning) {
        statusBar()->showMessage("A tool is running - please wait for it to finish.");
        return;
    }
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) {
        statusBar()->showMessage("No active profile.");
        return;
    }
    if (!solero::AppConfig::instance().isConfigured()) {
        statusBar()->showMessage("Game directory not configured. Use Game Settings...");
        return;
    }

    // Both branches run off the UI thread and finish asynchronously: all state
    // updates + the button/notice/sort/health refresh happen in the respective
    // watcher's finished handler, not here (this returns immediately).
    if (!m_deployed || m_deployDirty) {
        deployActiveAsync({});
    } else {
        auto ret = QMessageBox::question(this, "UnDeploy",
            "Remove all deployed mod links? Staged mods will not be affected.",
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
        undeployActiveAsync({});
    }
}

void MainWindow::updateDeployButton() {
    if (!m_deployAction) return;
    if (m_deployed && m_deployDirty) {
        m_deployAction->setText("\xe2\x9a\xa0 Redeploy");   // ⚠
        m_deployAction->setToolTip("Mod changes since last deploy - click to redeploy (Ctrl+D)");
    } else if (m_deployed) {
        m_deployAction->setText("\xe2\x9c\x93 Deployed");
        m_deployAction->setToolTip("Mods are deployed - click to undeploy (Ctrl+D)");
    } else {
        m_deployAction->setText("\xe2\x9c\x97 Not Deployed");
        m_deployAction->setToolTip("Click to deploy mods to game directory (Ctrl+D)");
    }
}

void MainWindow::refreshHealthIndicator() {
    // Debounce: enable/disable, rename and variant-switch each poke this; a burst of
    // them collapses into a single recompute rather than one shallow staging scan of
    // every enabled mod per event.
    if (!m_healthDebounce) {
        m_healthDebounce = new QTimer(this);
        m_healthDebounce->setSingleShot(true);
        m_healthDebounce->setInterval(150);
        connect(m_healthDebounce, &QTimer::timeout,
                this, &MainWindow::recomputeHealthIndicator);
    }
    m_healthDebounce->start();
}

void MainWindow::recomputeHealthIndicator() {
    if (!m_problemsBtn) return;
    // The health scan reads the active Profile's mod/plugin lists. While an
    // off-thread deploy is in flight the worker mutates them (LOOT reorders the
    // plugin list), so skip now - the deploy's finished handler re-triggers this.
    if (m_deploying) return;
    QList<solero::HealthIssue> issues;
    if (auto* profile = m_profileMgr->activeProfile()) {
        solero::HealthInputs in;
        in.dependencyWarnings = solero::DependencyChecker::check(
            profile->modList(), solero::AppConfig::instance().stagingDir());
        in.lastDeployWarning      = m_lastDeployWarning;
        in.lastDeployHadFailures  = m_lastDeployHadFailures;
        in.deployed               = m_deployed;
        in.deployDirty            = m_deployDirty;
        issues = solero::collect(*profile, m_lastConflicts, in);
    }

    // The Problems panel + indicator surface only actionable problems, so drop
    // Info-level notices (conflicts-resolved-by-load-order, deploy state) here -
    // collect() still computes them, the UI just ignores them. With only Info
    // items the list goes empty and the indicator reads "No Problems".
    issues.erase(std::remove_if(issues.begin(), issues.end(),
        [](const solero::HealthIssue& i) {
            return i.severity == solero::HealthSeverity::Info;
        }), issues.end());

    // Severity-coloured exclamation: grey (clean) / amber (warnings only) / red
    // (any error). Tooltip enumerates the counts so a hover explains the colour.
    int errors = 0, warnings = 0;
    for (const auto& i : issues) {
        if (i.severity == solero::HealthSeverity::Error)   ++errors;
        else if (i.severity == solero::HealthSeverity::Warning) ++warnings;
    }
    const QString dash = QStringLiteral(" ") + QChar('-') + QStringLiteral(" ");
    if (issues.isEmpty()) {
        m_problemsBtn->setIcon(solero::neutralSignIcon(20));
        m_problemsBtn->setToolTip(QStringLiteral("No problems") + dash + QStringLiteral("click for details"));
    } else {
        const bool hasError = errors > 0;
        m_problemsBtn->setIcon(hasError ? solero::errorSignIcon(20)
                                        : solero::warnSignIcon(20));
        QStringList parts;
        if (errors)   parts << QString("%1 error%2").arg(errors).arg(errors == 1 ? "" : "s");
        if (warnings) parts << QString("%1 warning%2").arg(warnings).arg(warnings == 1 ? "" : "s");
        m_problemsBtn->setToolTip(parts.join(QStringLiteral(", ")) + dash
                                  + QStringLiteral("click for details"));
    }

    // Push results into the detail panel whenever it exists (not only when
    // already visible) so the first open - which scans before show() - shows the
    // issues immediately instead of an empty list until a manual Rescan. The
    // rebuild is a cheap clear()+repopulate, harmless while hidden.
    if (m_problemsDialog)
        m_problemsDialog->setIssues(issues);
}

void MainWindow::onShowProblems() {
    if (!m_problemsDialog) {
        m_problemsDialog = new solero::ProblemsDialog(this);
        connect(m_problemsDialog, &solero::ProblemsDialog::rescanRequested,
                this, &MainWindow::recomputeHealthIndicator);
        connect(m_problemsDialog, &solero::ProblemsDialog::goToMod, this,
                [this](const QString& id) {
                    if (m_browseAction && m_browseAction->isChecked())
                        m_browseAction->setChecked(false); // leave the Nexus browser
                    m_modListView->selectModById(id);
                });
        connect(m_problemsDialog, &solero::ProblemsDialog::goToPlugin, this,
                [this](const QString& filename) {
                    m_rightPane->selectPlugin(filename);
                });
    }
    recomputeHealthIndicator(); // populate synchronously before showing
    m_problemsDialog->show();
    m_problemsDialog->raise();
    m_problemsDialog->activateWindow();
}

void MainWindow::onDataRename(const QString& modId, const QString& relPath,
                              const QString& newName, bool isFolder) {
    // relPath is relative to the mod's staging root, so it already carries the
    // "Data/…" prefix shown in the tree.
    const QString root = stagingRootForId(modId);
    const QString src  = root + "/" + relPath;
    const int slash = relPath.lastIndexOf('/');
    const QString parentRel = slash >= 0 ? relPath.left(slash + 1) : QString();
    const QString dst = root + "/" + parentRel + newName;

    const bool ok = isFolder ? QDir().rename(src, dst) : QFile::rename(src, dst);
    if (!ok) {
        QMessageBox::warning(this, "Rename Failed",
                             QString("Could not rename '%1'. The file may be in use or the staging folder may not be writable.").arg(relPath));
        return;
    }
    // Staged files for this mod changed - drop its cached scans and mark dirty.
    m_modListView->invalidateModCache(modId);
    m_rightPane->invalidateModPluginCache(modId);
    solero::DependencyChecker::invalidate(modId);
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    updatePluginNotice();
    refreshHealthIndicator();
}

void MainWindow::onVariantSwitched(const QString& modId) {
    // The mod's active version changed, so its staged files differ - drop cached
    // scans and rescan plugins for it (mirrors the install tail).
    m_modListView->invalidateModCache(modId);
    m_rightPane->invalidateModPluginCache(modId);
    solero::DependencyChecker::invalidate(modId);
    if (auto* p = m_profileMgr->activeProfile()) m_rightPane->refreshPlugins(p);
    updatePluginNotice();
    refreshHealthIndicator();
    // Auto-redeploy so the new version's files replace the old ones in the game
    // Data. If nothing is deployed yet, just mark dirty (deferred until deploy).
    if (m_deployed)
        redeployForTool("Switching version", QStringLiteral("Re-deploying") + QChar(0x2026));
    else { m_deployDirty = true; updateDeployButton(); }
}

void MainWindow::onDataDelete(const QString& modId, const QString& relPath,
                              bool isFolder) {
    const QString root = stagingRootForId(modId);
    const QString path = root + "/" + relPath;

    // Confirm before touching staged files - a folder delete can remove many
    // files at once. Default to Cancel so Enter can't delete by accident.
    const QString what = isFolder ? QString("the folder \"%1\" and everything in it").arg(relPath)
                                  : QString("\"%1\"").arg(relPath);
    QMessageBox confirm(QMessageBox::Question, "Delete Staged Files",
        QString("Delete %1 from this mod's staging?\nSolero will move it to the "
                "Trash if possible.").arg(what),
        QMessageBox::Cancel, this);
    auto* delBtn = confirm.addButton("Delete", QMessageBox::DestructiveRole);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != delBtn) return;

    // Staged files may be symlinks (Fluorine/Wabbajack): removing the link/dir
    // removes it from the mod without touching the source target.
    // Prefer the Trash so the delete is recoverable; fall back to a permanent
    // delete only if trashing isn't available (e.g. no trash on that mount).
    bool ok = QFile::moveToTrash(path);
    if (!ok)
        ok = isFolder ? QDir(path).removeRecursively()
                      : QFile::remove(path);
    if (!ok) {
        QMessageBox::warning(this, "Delete Failed",
                             QString("Could not delete '%1'. The file may be in use or the staging folder may not be writable.").arg(relPath));
        return;
    }
    m_modListView->invalidateModCache(modId);
    m_rightPane->invalidateModPluginCache(modId);
    solero::DependencyChecker::invalidate(modId);
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    updatePluginNotice();
    refreshHealthIndicator();
}

void MainWindow::updatePluginNotice() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { m_rightPane->hidePluginNotice(); return; }
    bool hasEnabledMods = false;
    for (const auto& m : profile->modList())
        if (m.type == solero::EntryType::Mod && m.enabled) { hasEnabledMods = true; break; }
    if (m_deployed && m_deployDirty)
        m_rightPane->showPluginNotice("\xe2\x9a\xa0 Mod changes haven't been deployed - redeploy to update this plugin list.");
    else if (!m_deployed && hasEnabledMods)
        m_rightPane->showPluginNotice("\xe2\x9a\xa0 Mods aren't deployed yet - deploy to update this plugin list.");
    else
        m_rightPane->hidePluginNotice();
}

void MainWindow::updateSortButton() {
    // LOOT reads plugin headers from the deployed Data folder, so sorting only
    // makes sense when deployed; only offer it when the user dirtied the order
    // and the load order isn't locked (locking disables LOOT auto-sort).
    auto* profile = m_profileMgr->activeProfile();
    const bool locked = profile && profile->pluginList().loadOrderLocked();
    m_rightPane->setSortButtonEnabled(
        m_deployed && m_loadOrderDirty && !locked,
        locked ? QStringLiteral("Load order is locked")
               : QStringLiteral("Run LOOT to auto-sort the load order"));
}

void MainWindow::onLockOrderToggled(bool checked) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    profile->pluginList().setLoadOrderLocked(checked);
    checkSave(profile->save());
    updateSortButton();
    statusBar()->showMessage(checked
        ? "Load order locked - LOOT auto-sort disabled; manual order kept."
        : "Load order unlocked - LOOT auto-sort re-enabled.");
}

void MainWindow::onLoadOrderChanged() {
    m_loadOrderDirty = true;
    updateSortButton();
    statusBar()->showMessage("Load order changed - Sort Now (LOOT) available.");
}

void MainWindow::onSortRequested() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile."); return; }
    if (profile->pluginList().loadOrderLocked()) {
        statusBar()->showMessage("Load order is locked - unlock to sort.");
        return;
    }
    if (!m_deployed) {
        statusBar()->showMessage("Deploy first - LOOT needs plugins in the game's Data folder.");
        return;
    }

    // Auto-snapshot the current (pre-sort) order so "Restore Load Order" can undo
    // a LOOT sort the user didn't like. Labeled with a timestamp for clarity.
    const QString preLabel = QStringLiteral("Auto (pre-LOOT %1)")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
    solero::LoadOrderBackup::create(*profile, preLabel);

    solero::ProgressModal prog(this, "Sort", "Sorting plugins with LOOT\xe2\x80\xa6");
    prog.show(); prog.pump();

    auto res = solero::LootSorter::sort(profile->pluginList(),
                                        solero::AppConfig::instance().gameDir(),
                                        profile->lootUserlistPath());
    prog.close();

    if (!res.success) {
        QMessageBox::warning(this, "LOOT Sort Failed",
            res.errorMessage.isEmpty()
                ? "LOOT could not sort plugins. Make sure the game is deployed first "
                  "(plugins need to be in Data/), then try Sort again."
                : "LOOT sort failed: " + stripLootPathPrefix(res.errorMessage));
        return;
    }

    profile->pluginList().applyPins(); // restore pinned plugins after the sort
    checkSave(profile->save());
    m_rightPane->setProfile(profile); // refresh the plugins view with the sorted order
    m_loadOrderDirty = false;
    updateSortButton();
    statusBar()->showMessage("Plugins sorted by LOOT.");
}

void MainWindow::onBackupLo() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile."); return; }

    bool ok = false;
    const QString def = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    const QString label = QInputDialog::getText(this, "Backup Load Order",
        "Label (optional):", QLineEdit::Normal, def, &ok);
    if (!ok) return;

    const QString path = solero::LoadOrderBackup::create(*profile, label);
    if (path.isEmpty())
        statusBar()->showMessage("Couldn't write load-order backup.");
    else
        statusBar()->showMessage("Load order backed up (" +
            QString::number(profile->pluginList().count()) + " plugins, " +
            QString::number(profile->modList().count()) + " mods).");
}

void MainWindow::onRestoreLo() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile."); return; }

    auto backups = solero::LoadOrderBackup::list(*profile);
    if (backups.isEmpty()) {
        QMessageBox::information(this, "Restore Load Order",
            "No load-order backups yet - use \"Backup LO\" first.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Restore Load Order");
    dlg.resize(460, 320);
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("Pick a snapshot to restore:", &dlg));
    auto* listw = new QListWidget(&dlg);
    layout->addWidget(listw, 1);

    auto populate = [&] {
        listw->clear();
        for (const auto& b : backups) {
            const QString when = b.created.isValid()
                ? b.created.toString("yyyy-MM-dd HH:mm") : QString("unknown time");
            // Legacy plugin-only backups (modCount < 0) omit the mod tally.
            const QString counts = b.modCount < 0
                ? QString("%1 plugins").arg(b.pluginCount)
                : QString("%1 plugins, %2 mods").arg(b.pluginCount).arg(b.modCount);
            auto* item = new QListWidgetItem(
                QString("%1  %2  %3  (%4)")
                    .arg(b.label, QString(QChar('-')), when, counts), listw);
            item->setData(Qt::UserRole, b.path);
        }
        if (listw->count()) listw->setCurrentRow(0);
    };
    populate();

    auto* btnRow = new QHBoxLayout();
    auto* deleteBtn = new QPushButton("Delete", &dlg);
    btnRow->addWidget(deleteBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    auto* buttons = new QDialogButtonBox(&dlg);
    auto* restoreBtn = buttons->addButton("Restore", QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    connect(deleteBtn, &QPushButton::clicked, &dlg, [&] {
        auto* item = listw->currentItem();
        if (!item) return;
        const QString path = item->data(Qt::UserRole).toString();
        QMessageBox confirm(QMessageBox::Question, "Delete backup",
                            "Delete \"" + item->text() + "\"?",
                            QMessageBox::Cancel, &dlg);
        auto* delBtn = confirm.addButton("Delete", QMessageBox::DestructiveRole);
        confirm.setDefaultButton(QMessageBox::Cancel); // Enter must not delete
        confirm.exec();
        if (confirm.clickedButton() != delBtn) return;
        QFile::remove(path);
        backups = solero::LoadOrderBackup::list(*profile);
        populate();
        if (backups.isEmpty()) dlg.reject();
    });

    auto syncEnabled = [&] { restoreBtn->setEnabled(listw->currentItem() != nullptr); };
    connect(listw, &QListWidget::currentRowChanged, &dlg, [&](int){ syncEnabled(); });
    syncEnabled();

    if (dlg.exec() != QDialog::Accepted) return;
    auto* item = listw->currentItem();
    if (!item) return;

    const auto snap = solero::LoadOrderBackup::load(item->data(Qt::UserRole).toString());
    if (!snap.valid) { statusBar()->showMessage("Couldn't read that backup."); return; }

    profile->pluginList().restoreSnapshot(snap.plugins);

    // Restore the mod list too when the backup carries one. Old
    // plugin-only backups leave hasMods false, so the live mod list is untouched.
    // setOrder reproduces the snapshot's ordering (unknown/new mods keep their
    // relative position, appended at the end); enabled flags are re-applied per
    // mod. Separators are ordered but never toggled.
    bool modsRestored = false;
    if (snap.hasMods) {
        QStringList order;
        for (const auto& e : snap.mods) order << e.id;
        profile->modList().setOrder(order);
        for (const auto& e : snap.mods)
            if (!e.isSeparator && profile->modList().findById(e.id))
                profile->modList().setEnabled(e.id, e.enabled);
        profile->modList().normalizeGroups(); // keep the group invariant after reordering
        modsRestored = true;
    }

    profile->save();
    m_rightPane->setProfile(profile); // refresh the plugins view with the restored order
    if (modsRestored) m_modListView->setProfile(profile); // rebuild the mod list too
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    updatePluginNotice();
    statusBar()->showMessage(modsRestored
        ? QString("Load order + mod list restored from \"%1\".").arg(snap.label)
        : QString("Load order restored from \"%1\".").arg(snap.label));
}

void MainWindow::onModsChanged() {
    auto* profile = m_profileMgr->activeProfile();
    if (profile) m_rightPane->refreshPlugins(profile);  // plugins follow enabled mods
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    updatePluginNotice();
    refreshHealthIndicator();
}

void MainWindow::onModsReordered() {
    // Order-only change: plugin membership and dependency health are unchanged, so
    // skip the full-staging plugin rescan (refreshPlugins) and dependency-health
    // walk (refreshHealthIndicator). Deploy order DID change, so still mark dirty.
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
}

void MainWindow::refreshCategoryFilter() {
    if (!m_categoryCombo || !m_modListView) return;
    const QString cur = m_categoryCombo->currentData().toString(); // preserve selection
    QSignalBlocker block(m_categoryCombo);                         // no spurious re-filter
    m_categoryCombo->clear();
    m_categoryCombo->addItem("All categories", QString());
    for (const QString& name : m_modListView->sectionNames())
        m_categoryCombo->addItem(name, name);
    const int idx = m_categoryCombo->findData(cur);
    m_categoryCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    // Re-assert the (possibly reset) selection on the view, since signals were
    // blocked and the previously-chosen section may have vanished.
    m_modListView->setCategoryFilter(m_categoryCombo->currentData().toString());
}

// recognised archive extensions for external drag-drop install. Matches
// the file-dialog filters used by onInstallMod / the downloads scan.
static bool isArchivePath(const QString& path) {
    static const QStringList kExts = {"zip", "7z", "rar", "tar", "gz"};
    return kExts.contains(QFileInfo(path).suffix().toLower());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) return;
    // Accept as soon as at least one dropped local file is a known archive.
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && isArchivePath(url.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) return;
    QStringList archives;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        const QString path = url.toLocalFile();
        if (isArchivePath(path)) archives << path; // ignore non-archive drops
    }
    if (archives.isEmpty()) return;
    event->acceptProposedAction();
    for (const QString& path : archives)
        installFromArchive(path);
}

// Defined later in this file; used by installFromArchive to pick a unique
// staging folder for a Keep Both / Replace version variant.
static QSet<QString> takenStagingFolders(solero::Profile* profile);

void MainWindow::onInstallMod() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile."); return; }
    if (!solero::AppConfig::instance().isConfigured()) {
        statusBar()->showMessage("Configure the game first (Game Settings...).");
        return;
    }

    const QString dl = solero::AppConfig::instance().downloadsDir();
    QString archive = QFileDialog::getOpenFileName(
        this, "Install Mod from Archive", dl.isEmpty() ? QDir::homePath() : dl,
        "Mod archives (*.zip *.7z *.rar *.tar *.gz);;All files (*)");
    if (archive.isEmpty()) return;
    installFromArchive(archive);
}

void MainWindow::installFromArchive(const QString& archive) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile."); return; }
    if (!solero::AppConfig::instance().isConfigured()) {
        statusBar()->showMessage("Configure the game first (Game Settings...).");
        return;
    }
    if (archive.isEmpty()) return;

    statusBar()->showMessage("Preparing...");
    qApp->processEvents();

    auto extractProg = std::make_unique<solero::ProgressModal>(this, "Install", "Extracting archive...");
    extractProg->show(); extractProg->pump();

    auto prep = solero::ModInstaller::prepare(archive, [&](int pct){ extractProg->setProgress(pct, 100); });
    if (!prep.ok) { extractProg->close(); QMessageBox::critical(this, "Install Failed", prep.errorMessage); return; }

    // Duplicate detection (MO2-style Replace / Rename)
    // Read any Nexus sidecar up front (also reused for tagging the mod below) so we
    // can recognise a re-install of a mod that already exists in this profile.
    QString sidecarModId, sidecarFileId, sidecarVersion, sidecarName, sidecarGame;
    {
        QFile sf(archive + ".solero-nexus.json");
        if (sf.open(QIODevice::ReadOnly)) {
            const QJsonObject meta = QJsonDocument::fromJson(sf.readAll()).object();
            sidecarModId   = meta["modId"].toString();
            sidecarFileId  = meta["fileId"].toString();
            sidecarVersion = meta["version"].toString();
            sidecarName    = meta["name"].toString();
            sidecarGame    = meta["game"].toString();
        }
    }
    // Prefer the real Nexus catalog name over the archive filename. Older sidecars
    // (and the nxm path) don't carry a name, so fetch it once here and cache it back
    // to the sidecar; the archive-filename clean is only a fallback for local/offline
    // installs with no Nexus identity.
    if (sidecarName.isEmpty() && !sidecarModId.isEmpty()) {
        const QString game = sidecarGame.isEmpty()
            ? QString(solero::NexusApi::kDefaultGame) : sidecarGame;
        const QString fetched = solero::NexusApi::modInfo(sidecarModId, game).name;
        if (!fetched.isEmpty()) {
            sidecarName = fetched;
            QFile sf(archive + ".solero-nexus.json");
            if (sf.open(QIODevice::ReadOnly)) {
                QJsonObject meta = QJsonDocument::fromJson(sf.readAll()).object();
                sf.close();
                meta["name"] = sidecarName;
                if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    sf.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));
            }
        }
    }
    // A Nexus name wins outright; otherwise strip Nexus's "-<modId>-..." filename tail.
    const QString proposedName = sidecarName.isEmpty()
        ? cleanModName(prep.modName, sidecarModId) : sidecarName;

    QString existingModId;     // non-empty -> Replace the existing mod in place
    int existingVariantIdx = -1; // ≥0 -> SameFile targets this variant of existingModId
    QString stagingOverride;   // full mod dir for Replace (migrated staging folder)
    QString overrideName;      // non-empty -> Rename: install new with this name
    bool installAsSibling = false; // different file of an already-installed mod
    QString variantTargetId;   // non-empty -> add/replace a version variant on this entry
    bool    variantReplace = false; // variant path: Replace active version vs Keep Both
    QString variantFolder;     // new unique staging folder name for the variant
    {
        auto& ml = profile->modList();
        // Classify the incoming Nexus identity (sidecar) against the mod list once.
        // SameFile -> silent in-place reinstall; SiblingFile -> silent grouped sibling;
        // VersionConflict -> Replace / Keep Both / Rename dialog; NoConflict -> the
        // name-collision fallback below.
        const auto conflict = solero::classifyInstallConflict(
            ml, sidecarModId, sidecarFileId, sidecarVersion);

        if (conflict.kind == solero::InstallConflictKind::SameFile) {
            // (modId,fileId) is the reliable identity: literally the same file. A
            // redownload pins the existing fileId, so it lands here -> silent Replace.
            if (solero::ModEntry* sameFile = ml.findById(conflict.targetEntryId)) {
                existingModId = sameFile->id;
                // The incoming fileId may belong to an INACTIVE variant. Stage into
                // THAT variant's folder (not the mirror = active variant's folder),
                // else we'd extract the wrong version's files over the active one and
                // break the mirror invariant. The tail writes back via updateVariant.
                const int vi = ml.variantIndexByFileId(sameFile->id, sidecarFileId);
                if (vi >= 0) {
                    existingVariantIdx = vi;
                    stagingOverride = solero::AppConfig::instance().stagingDir()
                                    + "/" + sameFile->variants[vi].stagingFolder;
                } else {
                    stagingOverride = solero::stagingPathFor(
                        solero::AppConfig::instance().stagingDir(), *sameFile);
                }
            }
        } else if (conflict.kind == solero::InstallConflictKind::SiblingFile) {
            // Different file of an already-installed mod at the same version (e.g. two
            // main files of one Nexus page). Not a duplicate - install as a grouped
            // sibling; the auto-group pass below nests it.
            installAsSibling = true;
        } else if (conflict.kind == solero::InstallConflictKind::VersionConflict) {
            // A different version of an already-installed mod: let the user decide.
            solero::ModEntry* existing = ml.findById(conflict.targetEntryId);
            extractProg->hide(); // step the progress modal aside for the prompt
            QMessageBox box(this);
            box.setWindowTitle("Different Version");
            box.setText(QString("\"%1\" is already installed as version %2.")
                            .arg(existing->name, existing->version));
            box.setInformativeText(QString(
                "You're installing version %1. Replace the installed version, keep "
                "both (switchable from the Version column), or install it as a "
                "separate mod?").arg(sidecarVersion));
            QPushButton* replaceBtn = box.addButton("Replace",   QMessageBox::AcceptRole);
            QPushButton* keepBtn    = box.addButton("Keep Both", QMessageBox::ActionRole);
            QPushButton* renameBtn  = box.addButton(QString::fromUtf8("Rename…"),
                                                    QMessageBox::ActionRole);
            box.addButton("Cancel", QMessageBox::RejectRole);
            box.setDefaultButton(keepBtn);
            box.exec();
            QAbstractButton* clicked = box.clickedButton();
            if (clicked == replaceBtn || clicked == keepBtn) {
                // Both extract into a new uniquely-named staging folder; the post-
                // install tail attaches it as a variant of the existing entry.
                variantTargetId = existing->id;
                variantReplace  = (clicked == replaceBtn);
                variantFolder   = solero::uniqueStagingFolder(
                    solero::sanitizeStagingFolder(
                        existing->name + " (" + sidecarVersion + ")"),
                    takenStagingFolders(profile));
                stagingOverride = solero::AppConfig::instance().stagingDir()
                                + "/" + variantFolder;
            } else if (clicked == renameBtn) {
                bool ok = false;
                QString nm = QInputDialog::getText(
                    this, "Rename Mod", "Mod name:", QLineEdit::Normal,
                    uniqueModName(existing->name + " " + sidecarVersion, profile), &ok);
                nm = nm.trimmed();
                if (!ok || nm.isEmpty()) {
                    extractProg->close();
                    statusBar()->showMessage("Install cancelled.");
                    return;
                }
                // Install as a separate, grouped sibling under the chosen name.
                overrideName    = nm;
                installAsSibling = true;
            } else {
                extractProg->close();
                statusBar()->showMessage("Install cancelled.");
                return;
            }
            extractProg->show(); extractProg->pump();
        } else {
            // No Nexus identity (or a genuinely new mod): fall back to name collision.
            solero::ModEntry* collide = ml.findByName(proposedName);
            if (collide) {
                extractProg->hide(); // step the progress modal aside for the prompt
                QMessageBox box(this);
                box.setWindowTitle("Mod Already Exists");
                box.setText(QString("A mod named \"%1\" already exists.").arg(collide->name));
                box.setInformativeText("Replace it in place, or install as a new copy?");
                QPushButton* replaceBtn = box.addButton("Replace", QMessageBox::AcceptRole);
                QPushButton* renameBtn  = box.addButton("Rename",  QMessageBox::ActionRole);
                box.addButton("Cancel", QMessageBox::RejectRole);
                box.setDefaultButton(replaceBtn);
                box.exec();
                if (box.clickedButton() == replaceBtn) {
                    existingModId = collide->id;
                    stagingOverride = solero::stagingPathFor(
                        solero::AppConfig::instance().stagingDir(), *collide);
                } else if (box.clickedButton() == renameBtn) {
                    overrideName = uniqueModName(proposedName, profile);
                } else {
                    extractProg->close();
                    statusBar()->showMessage("Install cancelled.");
                    return;
                }
                extractProg->show(); extractProg->pump();
            }
        }
    }

    const QString staging = solero::AppConfig::instance().stagingDir();
    solero::InstallResult result;
    QJsonArray choiceLog;

    if (prep.layout.isFomod && !prep.fomodConfigPath.isEmpty()) {
        solero::FomodEngine engine;
        if (!engine.load(prep.fomodConfigPath)) {
            extractProg->close();
            QMessageBox::warning(this, "FOMOD", "Could not parse the FOMOD config; installing all files.");
            solero::ProgressModal stageProg(this, "Install", "Installing files...");
            stageProg.show(); stageProg.pump();
            result = solero::ModInstaller::stageSimple(prep, staging, existingModId,
                [&](int pct){ stageProg.setProgress(pct, 100); }, stagingOverride);
            stageProg.close();
        } else {
            // Extract image directories referenced by the FOMOD so the wizard can show them.
            QStringList imgDirs;
            for (const auto& step : engine.module().steps)
                for (const auto& grp : step.groups)
                    for (const auto& opt : grp.options) {
                        if (opt.imagePath.isEmpty()) continue;
                        QString p = opt.imagePath; p.replace('\\', '/');
                        int slash = p.indexOf('/');
                        QString top = slash < 0 ? p : p.left(slash);
                        if (!imgDirs.contains(top, Qt::CaseInsensitive)) imgDirs << top;
                    }
            if (!imgDirs.isEmpty() && !prep.fullyExtracted)
                solero::ModInstaller::extractSubpaths(prep, imgDirs, [&](int pct){ extractProg->setProgress(pct, 100); });
            extractProg->close();
            engine.setFilePresent([this](const QString& file) -> bool {
                // Present if the plugin file is in the live game Data dir, or in
                // any enabled mod's staged Data dir (case-insensitive by name).
                QString dataDir = solero::AppConfig::instance().gameDir() + "/Data";
                auto ciExists = [](const QString& dir, const QString& name){
                    QDir d(dir);
                    for (const QString& e : d.entryList(QDir::Files))
                        if (e.compare(name, Qt::CaseInsensitive) == 0) return true;
                    return false; };
                if (ciExists(dataDir, file)) return true;
                auto* p = m_profileMgr->activeProfile();
                if (p) for (const auto& m : p->modList())
                    if (m.type == solero::EntryType::Mod && m.enabled
                        && ciExists(solero::stagingPathFor(solero::AppConfig::instance().stagingDir(), m) + "/Data", file))
                        return true;
                return false;
            });
            solero::FomodWizard wizard(&engine, prep.fomodBase, this);
            if (wizard.exec() != QDialog::Accepted) { statusBar()->showMessage("Install cancelled."); return; }
            solero::ProgressModal stageProg(this, "Install", "Installing files...");
            stageProg.show(); stageProg.pump();
            result = solero::ModInstaller::stageFomod(prep, staging, wizard.result(), existingModId,
                [&](int pct){ stageProg.setProgress(pct, 100); }, stagingOverride);
            stageProg.close();
            const auto sel = wizard.selection();
            const auto& mod = engine.module();
            for (int si = 0; si < mod.steps.size(); ++si) {
                QJsonArray picks;
                for (int gi = 0; gi < mod.steps[si].groups.size(); ++gi)
                    for (int oi = 0; oi < mod.steps[si].groups[gi].options.size(); ++oi)
                        if (sel.value(solero::FomodEngine::selKey(si, gi, oi)))
                            picks.append(mod.steps[si].groups[gi].options[oi].name);
                QJsonObject stepObj;
                stepObj["step"] = mod.steps[si].name;
                stepObj["selected"] = picks;
                choiceLog.append(stepObj);
            }
        }
    } else {
        extractProg->close();
        solero::ProgressModal stageProg(this, "Install", "Installing files...");
        stageProg.show(); stageProg.pump();
        result = solero::ModInstaller::stageSimple(prep, staging, existingModId,
            [&](int pct){ stageProg.setProgress(pct, 100); }, stagingOverride);
        stageProg.close();
    }

    if (!result.success) { QMessageBox::critical(this, "Install Failed", result.errorMessage); return; }

    if (!choiceLog.isEmpty()) {
        QJsonObject root;
        root["installer_version"] = "1.0";
        root["installed_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        root["steps"] = choiceLog;
        QString fomodDir = solero::AppConfig::dataRoot() + "/fomod-choices";
        QDir().mkpath(fomodDir);
        QString cp = fomodDir + "/" + result.modId + ".json";
        QFile f(cp);
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }

    if (!variantTargetId.isEmpty()) {
        // Keep Both / Replace a version variant
        // The archive (+ FOMOD picks) landed in `variantFolder`; attach it to the
        // existing entry as a variant instead of creating a new ModEntry.
        solero::ModVariant nv{sidecarVersion, sidecarFileId, variantFolder, archive,
                              !choiceLog.isEmpty()};
        if (variantReplace) {
            QString retired;
            profile->modList().replaceActiveVersion(variantTargetId, nv, &retired);
            checkSave(profile->save());
            // The user explicitly chose Replace and the new install already landed,
            // so it's safe to drop the retired version's staging folder now.
            if (!retired.isEmpty()) {
                QDir retiredDir(solero::AppConfig::instance().stagingDir() + "/" + retired);
                if (retiredDir.exists() && !retiredDir.removeRecursively())
                    statusBar()->showMessage(
                        "Replaced the version, but the old version's files couldn't "
                        "be removed - you can delete them manually from the staging "
                        "directory.", 6000);
            }
        } else {
            profile->modList().keepBothAddVariant(variantTargetId, nv);
            checkSave(profile->save());
        }
    } else if (!existingModId.isEmpty()) {
        // Replace in place
        // stageSimple/stageFomod reused the existing mod's UUID and replaced its
        // files; keep the entry's load-order position + enabled state, just refresh
        // its archive/Nexus metadata (mirrors onReinstallMod).
        if (solero::ModEntry* existing = profile->modList().findById(existingModId)) {
            if (existingVariantIdx >= 0 && !existing->variants.isEmpty()) {
                // The reinstall targeted a specific variant (its fileId owned it).
                // Write version/fileId/archive/fomod into THAT variant via
                // updateVariant, which re-syncs the mirrors only when it's the active
                // variant - never poke the mirrors directly (that desyncs variants[]).
                solero::ModVariant nv = existing->variants[existingVariantIdx];
                if (!sidecarVersion.isEmpty()) nv.version     = sidecarVersion;
                if (!sidecarFileId.isEmpty())  nv.nexusFileId = sidecarFileId;
                nv.sourceArchive   = archive;
                nv.hasFomodChoices = !choiceLog.isEmpty();
                // stagingFolder unchanged: we restaged into the variant's own folder.
                profile->modList().updateVariant(existingModId, existingVariantIdx, nv);
                // Entry-level (non-variant) metadata still refreshes.
                if (!sidecarModId.isEmpty()) existing->nexusModId = sidecarModId;
                existing->name = sidecarName.isEmpty()
                    ? cleanModName(existing->name, existing->nexusModId) : sidecarName;
            } else {
                existing->hasFomodChoices = !choiceLog.isEmpty();
                existing->sourceArchive   = archive;
                if (!sidecarModId.isEmpty())   existing->nexusModId  = sidecarModId;
                if (!sidecarFileId.isEmpty())  existing->nexusFileId = sidecarFileId;
                if (!sidecarVersion.isEmpty()) existing->version     = sidecarVersion;
                existing->name = sidecarName.isEmpty()
                    ? cleanModName(existing->name, existing->nexusModId) : sidecarName;
            }
        }
        checkSave(profile->save());
    } else {
        // Install as a new mod
        solero::ModEntry mod;
        mod.type = solero::EntryType::Mod;
        mod.id = result.modId;
        mod.name = result.modName;
        mod.enabled = true;
        mod.hasFomodChoices = !choiceLog.isEmpty();
        mod.sourceArchive = archive;
        // Tag the mod with the Nexus ids read from the sidecar up front (used by
        // endorse + the update checker).
        mod.nexusModId  = sidecarModId;
        mod.nexusFileId = sidecarFileId;
        if (!sidecarVersion.isEmpty()) mod.version = sidecarVersion;
        // Prefer the real Nexus name; else strip the "-<modId>-<version>-<timestamp>"
        // tail Nexus appends to archive names, now that mod.nexusModId is known.
        mod.name = sidecarName.isEmpty()
            ? cleanModName(mod.name, mod.nexusModId) : sidecarName;
        // A sibling (another file of an already-installed mod) would otherwise take
        // the shared catalog name. Name it from its archive instead so the grouped
        // files stay distinguishable, and de-dup against existing names.
        if (installAsSibling) {
            const QString fromArchive = cleanModName(result.modName, mod.nexusModId);
            mod.name = uniqueModName(
                fromArchive.isEmpty() ? mod.name : fromArchive, profile);
        }
        // Rename-on-collision -> use the de-duplicated name chosen above.
        if (!overrideName.isEmpty()) mod.name = overrideName;
        profile->modList().append(mod);

        // Auto-group: if another installed mod shares this Nexus mod id, nest the new
        // file under that mod's group (the existing mod becomes the group head, or, if
        // it's already a child, we nest under its parent). Children are stored
        // contiguously right after the parent - groupUnder handles the repositioning.
        // (A same-name/same-mod re-install never reaches here - it went through the
        // Replace branch above - so this only fires for genuine multi-file additions.)
        if (!mod.nexusModId.isEmpty()) {
            const auto& list = profile->modList();
            // The new mod was appended at the END, so it lives in the trailing
            // separator section (after the last separator). Only auto-group it under
            // a candidate that lives in that same section; grouping across a separator
            // would pull the child out of its section and break the "children are
            // contiguous with their parent in the same section" invariant.
            int lastSeparatorRaw = -1;
            for (int i = 0; i < list.count(); ++i)
                if (list.at(i).type == solero::EntryType::Separator)
                    lastSeparatorRaw = i;

            QString parentId;
            for (int i = 0; i < list.count(); ++i) {
                const auto& e = list.at(i);
                if (e.type != solero::EntryType::Mod) continue;
                if (e.id == mod.id) continue;
                if (e.nexusModId != mod.nexusModId) continue;
                // Same trailing section only (a candidate in the trailing section has
                // its parent, if any, in that same section too).
                if (i <= lastSeparatorRaw) continue;
                // Group head = the existing mod's parent if it's a child, else itself.
                parentId = e.parentId.isEmpty() ? e.id : e.parentId;
                break;
            }
            if (!parentId.isEmpty())
                profile->modList().groupUnder(mod.id, parentId);
        }

        // If this mod was just installed to satisfy another mod's requirement, move
        // it to sit directly above that dependent (anchor-by-identity reorder).
        if (!mod.nexusModId.isEmpty() && m_placeAboveByModId.contains(mod.nexusModId)) {
            const QString depId = m_placeAboveByModId.take(mod.nexusModId);
            auto& ml = profile->modList();
            if (ml.findById(depId)) {
                int from = -1, to = -1;
                for (int i = 0; i < ml.count(); ++i) {
                    if (ml.at(i).id == mod.id)  from = i;
                    if (ml.at(i).id == depId)   to = i;
                }
                if (from >= 0 && to >= 0 && from != to)
                    ml.reorder({from}, to); // insert just before the dependent
            }
        }
        checkSave(profile->save());
    }

    // Newly staged files for this mod - drop its cached empty/plugin scans. On the
    // variant path result.modId is a throwaway UUID (no ModEntry was created); the
    // entry whose staged files actually changed is the variant target.
    const QString refreshId =
        variantTargetId.isEmpty() ? result.modId : variantTargetId;
    m_modListView->invalidateModCache(refreshId);
    m_rightPane->invalidateModPluginCache(refreshId);
    solero::DependencyChecker::invalidate(refreshId);

    m_modListView->setProfile(profile);
    if (auto* p = m_profileMgr->activeProfile()) m_rightPane->refreshPlugins(p);
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    m_rightPane->downloadsTab()->refresh();
    updatePluginNotice();
    if (!variantTargetId.isEmpty()) {
        const solero::ModEntry* e = profile->modList().findById(variantTargetId);
        const QString verb = variantReplace ? "Replaced version of: " : "Added version to: ";
        statusBar()->showMessage(verb + (e ? e->name : result.modName));
    } else if (!existingModId.isEmpty()) {
        const solero::ModEntry* e = profile->modList().findById(existingModId);
        statusBar()->showMessage("Replaced: " + (e ? e->name : result.modName));
    } else {
        statusBar()->showMessage(QString("Installed: %1").arg(result.modName));
    }

    // A freshly-installed Community Shaders triggers the one-time managed-cache
    // offer (no-op if CS isn't present, already managed, or previously declined).
    maybeOfferShaderCacheManagement();

    // If this install satisfied a requirement listed in an open RequirementsDialog,
    // flip that row from "Installing…" to "Installed".
    if (!sidecarModId.isEmpty())
        markRequirementResult(sidecarModId, /*installed=*/true);

    // Fresh Nexus install: check its requirements and offer any that are missing.
    // (Skip Replace/reinstall and Keep Both/Replace variants - they update a mod
    // already present - to avoid nagging on every update.)
    if (existingModId.isEmpty() && variantTargetId.isEmpty() && !sidecarModId.isEmpty())
        checkRequirementsAfterInstall(profile, result.modId, sidecarModId, sidecarGame);
}

void MainWindow::onReinstallMod(const QString& modId) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    // Find the existing entry.
    solero::ModEntry* existing = profile->modList().findById(modId);
    if (!existing) return;

    QString archive = existing->sourceArchive;
    if (archive.isEmpty() || !QFile::exists(archive)) {
        // The stored archive is missing - try to locate the download automatically
        // before falling back to a manual file picker.
        const QStringList cands = findDownloadArchivesFor(existing);
        if (cands.size() == 1) {
            archive = cands.first();
        } else if (cands.size() > 1) {
            QStringList names;
            for (const QString& c : cands) names << QFileInfo(c).fileName();
            bool okSel = false;
            const QString chosen = QInputDialog::getItem(this, "Reinstall",
                QString("Found %1 possible archives for \"%2\". Choose one:")
                    .arg(cands.size()).arg(existing->name),
                names, 0, /*editable=*/false, &okSel);
            if (!okSel) return;
            archive = cands.at(names.indexOf(chosen));
        } else {
            archive.clear(); // none found -> try auto-download, else manual picker below
        }
        // No archive on disk: if the mod has a Nexus identity, try to re-download it
        // automatically (Premium API). On success we enqueue and RETURN - the
        // download-finished handler writes the sidecar and re-invokes onReinstallMod,
        // at which point findDownloadArchivesFor() locates the freshly-downloaded
        // file by its (modId, fileId) sidecar and the install proceeds normally.
        if (archive.isEmpty()
            && !existing->nexusModId.isEmpty() && !existing->nexusFileId.isEmpty()) {
            const QString url =
                solero::NexusApi::downloadUrl(existing->nexusModId, existing->nexusFileId);
            if (!url.isEmpty()) {
                // Resolve the real filename for this file from the Nexus file list;
                // fall back to the stored archive basename if the list is unavailable.
                QString fileName;
                for (const auto& f : solero::NexusApi::files(existing->nexusModId))
                    if (f.fileId == existing->nexusFileId) { fileName = f.name; break; }
                if (fileName.isEmpty() && !existing->sourceArchive.isEmpty())
                    fileName = QFileInfo(existing->sourceArchive).fileName();
                if (!fileName.isEmpty()) {
                    // Sidecar metadata so the re-entered reinstall finds the archive
                    // by (modId, fileId) and so the download tags it correctly.
                    m_nxmMeta[fileName] = QJsonObject{
                        {"game",   solero::NexusApi::kDefaultGame},
                        {"modId",  existing->nexusModId},
                        {"fileId", existing->nexusFileId},
                        {"version", existing->version},
                    };
                    m_pendingReinstalls[fileName] = modId;
                    m_downloads->enqueue(url, fileName,
                                         solero::AppConfig::instance().downloadsDir());
                    m_rightPane->showDownloadsTab();
                    statusBar()->showMessage(
                        QString::fromUtf8("Downloading %1\xE2\x80\xA6").arg(fileName));
                    return; // async: finished-handler re-enters onReinstallMod
                }
            } else {
                // Couldn't get a link (non-Premium / API error) - tell the user, then
                // fall through to the manual picker below (don't block).
                QMessageBox::information(this, "Reinstall",
                    "Couldn't auto-download this mod's archive (Nexus Premium API "
                    "required). Choose the archive manually instead.");
            }
        }
        if (archive.isEmpty())
            archive = QFileDialog::getOpenFileName(this, "Reinstall: choose the mod archive",
                solero::AppConfig::instance().downloadsDir().isEmpty() ? QDir::homePath()
                    : solero::AppConfig::instance().downloadsDir(),
                "Mod archives (*.zip *.7z *.rar *.tar *.gz);;All files (*)");
    }
    if (archive.isEmpty()) return;
    // Persist the located/chosen archive so future reinstalls skip the search.
    if (archive != existing->sourceArchive) {
        if (!existing->variants.isEmpty()) {
            // Route the mirror write through the active variant so variants[] stays
            // in sync (updateVariant re-syncs the mirrors for the active index).
            solero::ModVariant nv = existing->variants[existing->activeVariant];
            nv.sourceArchive = archive;
            profile->modList().updateVariant(modId, existing->activeVariant, nv);
        } else {
            existing->sourceArchive = archive;
        }
        checkSave(profile->save());
    }

    statusBar()->showMessage("Preparing...");
    qApp->processEvents();

    auto extractProg = std::make_unique<solero::ProgressModal>(this, "Reinstall", "Extracting archive...");
    extractProg->show(); extractProg->pump();

    auto prep = solero::ModInstaller::prepare(archive, [&](int pct){ extractProg->setProgress(pct, 100); });
    if (!prep.ok) { extractProg->close(); QMessageBox::critical(this, "Reinstall Failed", prep.errorMessage); return; }

    const QString staging = solero::AppConfig::instance().stagingDir();
    // Reinstall writes into the existing mod's real on-disk dir (which may have
    // been migrated from its UUID to its human staging-folder name), so resolve it.
    const QString stagingOverride = solero::stagingPathFor(staging, *existing);
    solero::InstallResult result;
    QJsonArray choiceLog;

    if (prep.layout.isFomod && !prep.fomodConfigPath.isEmpty()) {
        solero::FomodEngine engine;
        if (!engine.load(prep.fomodConfigPath)) {
            extractProg->close();
            solero::ProgressModal stageProg(this, "Reinstall", "Installing files...");
            stageProg.show(); stageProg.pump();
            result = solero::ModInstaller::stageSimple(prep, staging, modId,
                [&](int pct){ stageProg.setProgress(pct, 100); }, stagingOverride);
            stageProg.close();
        } else {
            // Extract image directories referenced by the FOMOD so the wizard can show them.
            QStringList imgDirs;
            for (const auto& step : engine.module().steps)
                for (const auto& grp : step.groups)
                    for (const auto& opt : grp.options) {
                        if (opt.imagePath.isEmpty()) continue;
                        QString p = opt.imagePath; p.replace('\\', '/');
                        int slash = p.indexOf('/');
                        QString top = slash < 0 ? p : p.left(slash);
                        if (!imgDirs.contains(top, Qt::CaseInsensitive)) imgDirs << top;
                    }
            if (!imgDirs.isEmpty() && !prep.fullyExtracted)
                solero::ModInstaller::extractSubpaths(prep, imgDirs, [&](int pct){ extractProg->setProgress(pct, 100); });
            extractProg->close();
            engine.setFilePresent([this](const QString& file) -> bool {
                // Present if the plugin file is in the live game Data dir, or in
                // any enabled mod's staged Data dir (case-insensitive by name).
                QString dataDir = solero::AppConfig::instance().gameDir() + "/Data";
                auto ciExists = [](const QString& dir, const QString& name){
                    QDir d(dir);
                    for (const QString& e : d.entryList(QDir::Files))
                        if (e.compare(name, Qt::CaseInsensitive) == 0) return true;
                    return false; };
                if (ciExists(dataDir, file)) return true;
                auto* p = m_profileMgr->activeProfile();
                if (p) for (const auto& m : p->modList())
                    if (m.type == solero::EntryType::Mod && m.enabled
                        && ciExists(solero::stagingPathFor(solero::AppConfig::instance().stagingDir(), m) + "/Data", file))
                        return true;
                return false;
            });
            solero::FomodWizard wizard(&engine, prep.fomodBase, this);
            // Pre-tick + label the user's previous FOMOD choices, if any were saved.
            {
                const QString choicePath =
                    solero::AppConfig::dataRoot() + "/fomod-choices/" + modId + ".json";
                QFile cf(choicePath);
                if (cf.open(QIODevice::ReadOnly)) {
                    const QJsonObject saved =
                        QJsonDocument::fromJson(cf.readAll()).object();
                    const solero::FomodPreset preset =
                        solero::buildFomodPreset(engine.module(), saved);
                    if (!preset.selection.isEmpty())
                        wizard.setPresetSelection(preset.selection, preset.priorKeys);
                }
            }
            if (wizard.exec() != QDialog::Accepted) { statusBar()->showMessage("Reinstall cancelled."); return; }
            solero::ProgressModal stageProg(this, "Reinstall", "Installing files...");
            stageProg.show(); stageProg.pump();
            result = solero::ModInstaller::stageFomod(prep, staging, wizard.result(), modId,
                [&](int pct){ stageProg.setProgress(pct, 100); }, stagingOverride);
            stageProg.close();
            const auto sel = wizard.selection();
            const auto& mod = engine.module();
            for (int si = 0; si < mod.steps.size(); ++si) {
                QJsonArray picks;
                for (int gi = 0; gi < mod.steps[si].groups.size(); ++gi)
                    for (int oi = 0; oi < mod.steps[si].groups[gi].options.size(); ++oi)
                        if (sel.value(solero::FomodEngine::selKey(si, gi, oi)))
                            picks.append(mod.steps[si].groups[gi].options[oi].name);
                QJsonObject stepObj; stepObj["step"] = mod.steps[si].name; stepObj["selected"] = picks;
                choiceLog.append(stepObj);
            }
        }
    } else {
        extractProg->close();
        solero::ProgressModal stageProg(this, "Reinstall", "Installing files...");
        stageProg.show(); stageProg.pump();
        result = solero::ModInstaller::stageSimple(prep, staging, modId,
            [&](int pct){ stageProg.setProgress(pct, 100); }, stagingOverride);
        stageProg.close();
    }

    if (!result.success) { QMessageBox::critical(this, "Reinstall Failed", result.errorMessage); return; }

    if (!choiceLog.isEmpty()) {
        QJsonObject root;
        root["installer_version"] = "1.0";
        root["installed_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        root["steps"] = choiceLog;
        QString fomodDir = solero::AppConfig::dataRoot() + "/fomod-choices";
        QDir().mkpath(fomodDir);
        QString cp = fomodDir + "/" + modId + ".json";
        QFile f(cp);
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
    if (!existing->variants.isEmpty()) {
        // Route the mirror writes through the active variant so variants[] stays in
        // sync (updateVariant re-syncs the mirrors for the active index).
        solero::ModVariant nv = existing->variants[existing->activeVariant];
        nv.sourceArchive   = archive;
        nv.hasFomodChoices = !choiceLog.isEmpty();
        profile->modList().updateVariant(modId, existing->activeVariant, nv);
    } else {
        existing->hasFomodChoices = !choiceLog.isEmpty();
        existing->sourceArchive = archive;
    }
    checkSave(profile->save());

    // Restaged files for this mod - drop its cached empty/plugin scans.
    m_modListView->invalidateModCache(modId);
    m_rightPane->invalidateModPluginCache(modId);
    solero::DependencyChecker::invalidate(modId);

    m_modListView->setProfile(profile);
    if (auto* p = m_profileMgr->activeProfile()) m_rightPane->refreshPlugins(p);
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    updatePluginNotice();
    statusBar()->showMessage("Reinstalled: " + existing->name);
}

QStringList MainWindow::findDownloadArchivesFor(const solero::ModEntry* existing) const {
    QStringList out;
    if (!existing) return out;
    const QString dl = solero::AppConfig::instance().downloadsDir();
    if (dl.isEmpty()) return out;
    static const QStringList kExts = {"*.zip", "*.7z", "*.rar", "*.tar", "*.gz"};
    const QFileInfoList files = QDir(dl).entryInfoList(kExts, QDir::Files, QDir::Time);
    auto addUnique = [&out](const QString& p) {
        if (!p.isEmpty() && !out.contains(p)) out.append(p);
    };

    // 1) Exact basename of the stored sourceArchive (path may have moved/changed).
    if (!existing->sourceArchive.isEmpty()) {
        const QString base = QFileInfo(existing->sourceArchive).fileName();
        for (const QFileInfo& fi : files)
            if (fi.fileName().compare(base, Qt::CaseInsensitive) == 0)
                addUnique(fi.absoluteFilePath());
    }

    // 2) Nexus sidecar whose modId matches (and fileId, when the mod has one).
    if (!existing->nexusModId.isEmpty()) {
        for (const QFileInfo& fi : files) {
            QFile sf(fi.absoluteFilePath() + ".solero-nexus.json");
            if (!sf.open(QIODevice::ReadOnly)) continue;
            const QJsonObject meta = QJsonDocument::fromJson(sf.readAll()).object();
            if (meta["modId"].toString() != existing->nexusModId) continue;
            if (!existing->nexusFileId.isEmpty()
                && meta["fileId"].toString() != existing->nexusFileId) continue;
            addUnique(fi.absoluteFilePath());
        }
    }

    // 3) Fuzzy: the mod name (normalized) is a substring of the archive name
    //    (or vice-versa). Require a reasonably long name to avoid weak matches.
    const QString target = normalizeName(existing->name);
    if (target.size() >= 4) {
        for (const QFileInfo& fi : files) {
            const QString cand = normalizeName(fi.completeBaseName());
            if (cand.isEmpty()) continue;
            if (cand.contains(target) || target.contains(cand))
                addUnique(fi.absoluteFilePath());
        }
    }
    return out;
}

QString MainWindow::uniqueModName(const QString& base, solero::Profile* profile) const {
    if (!profile) return base;
    auto& ml = profile->modList();
    if (!ml.findByName(base)) return base;
    for (int n = 2; ; ++n) {
        const QString cand = QString("%1 (%2)").arg(base).arg(n);
        if (!ml.findByName(cand)) return cand;
    }
}

// Gather the set of staging folder names already taken in a profile
// (case-insensitive), so a newly created mod can be assigned a unique folder.
static QSet<QString> takenStagingFolders(solero::Profile* profile) {
    QSet<QString> taken;
    if (!profile) return taken;
    for (const auto& e : profile->modList()) {
        if (e.type != solero::EntryType::Mod) continue;
        if (!e.stagingFolder.isEmpty())
            taken.insert(e.stagingFolder.toLower());
        // Inactive Keep-Both variants own their own on-disk folders too; reserve
        // them so a new folder name can't collide with a non-active variant.
        for (const auto& v : e.variants)
            if (!v.stagingFolder.isEmpty())
                taken.insert(v.stagingFolder.toLower());
    }
    // The managed shader cache lives outside the list but owns a folder per CS
    // version - reserve every key's folder, not just the active one.
    for (const QString& f : profile->shaderCache().folders.values())
        if (!f.isEmpty())
            taken.insert(f.toLower());
    return taken;
}

// On-disk staging path of the profile's managed shader cache, or empty if inactive.
static QString cacheStagingPath(solero::Profile* profile) {
    if (!profile || !profile->shaderCache().active()) return {};
    const QString folder = profile->shaderCache().folderFor(
        solero::activeCacheKey(profile->modList()));
    if (folder.isEmpty()) return {};
    return solero::AppConfig::instance().stagingDir() + "/" + folder;
}

QString MainWindow::stagingRootForId(const QString& modId) const {
    const QString stagingDir = solero::AppConfig::instance().stagingDir();
    if (auto* p = m_profileMgr->activeProfile())
        return stagingDir + "/" + p->stagingFolderFor(modId);
    return stagingDir + "/" + modId; // no profile: fall back to the id
}

void MainWindow::migrateLegacyOverwrite() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    const QString base = solero::AppConfig::dataRoot() + "/overwrite";
    QDir baseDir(base);
    if (!baseDir.exists()) return;

    // Sanitized names of all known profiles - their subdirs are already per-profile
    // Overwrite folders and must be left alone (never moved into another profile).
    QSet<QString> profileDirs;
    for (const QString& n : m_profileMgr->profileNames()) {
        QString safe = n; safe.replace('/', '_').replace('\\', '_');
        profileDirs.insert(safe);
    }

    const auto entries = baseDir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    const QString dest = solero::AppConfig::overwriteDir(profile->name());
    int moved = 0;
    bool madeDest = false;
    for (const QFileInfo& fi : entries) {
        if (fi.isDir() && profileDirs.contains(fi.fileName())) continue; // already per-profile
        if (!madeDest) { QDir().mkpath(dest); madeDest = true; }
        const QString target = dest + "/" + fi.fileName();
        if (!QFileInfo::exists(target)) {
            if (QDir().rename(fi.absoluteFilePath(), target)) ++moved;
        } else if (fi.isDir()) {
            // Merge into an existing same-named dir, then drop the emptied source.
            moved += solero::moveTreeContents(fi.absoluteFilePath(), target);
            QDir(fi.absoluteFilePath()).removeRecursively();
        } // existing file: keep the profile's copy, leave the legacy one in place
    }
    if (moved > 0)
        statusBar()->showMessage(
            QString("Moved %1 legacy Overwrite item(s) into profile '%2'.")
                .arg(moved).arg(profile->name()));
}

void MainWindow::onCreateModFromOverwrite() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile."); return; }

    const QString overwriteDir = solero::AppConfig::overwriteDir(profile->name());
    // The context action is disabled when Overwrite is empty, but guard anyway.
    {
        QDirIterator probe(overwriteDir,
                           QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                           QDirIterator::Subdirectories);
        if (!probe.hasNext()) { statusBar()->showMessage("Overwrite is empty."); return; }
    }

    bool ok = false;
    QString name = QInputDialog::getText(this, "Create Mod from Overwrite",
        "Mod name:", QLineEdit::Normal, "Overwrite Mod", &ok);
    if (!ok) return;
    name = name.trimmed();
    if (name.isEmpty()) return;
    // Auto-suffix " (2)" etc. so the new mod never collides with an existing name.
    name = uniqueModName(name, profile);

    // A fresh real mod + its staging Data dir (mirrors ensureOutputMod's layout).
    solero::ModEntry mod;
    mod.type = solero::EntryType::Mod;
    mod.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mod.name = name;
    mod.enabled = true;
    mod.stagingFolder = solero::uniqueStagingFolder(
        solero::sanitizeStagingFolder(name), takenStagingFolders(profile));

    const QString destData = solero::stagingPathFor(
        solero::AppConfig::instance().stagingDir(), mod) + "/Data";
    QDir().mkpath(destData);

    // Move (not copy) the overwrite contents in; cross-fs safe, leaves Overwrite empty.
    const int moved = solero::moveTreeContents(overwriteDir, destData);

    // Append -> the new mod lands at the bottom of the real mod list, i.e. just
    // ABOVE the Overwrite pseudo-row (which isn't stored in the mod list).
    profile->modList().append(mod);
    profile->save();

    m_modListView->invalidateModCache(mod.id); // newly staged files for this mod
    m_modListView->invalidateModCache();        // Overwrite contents changed (now empty)
    m_rightPane->invalidateModPluginCache(mod.id);
    solero::DependencyChecker::invalidate(mod.id);
    m_modListView->setProfile(profile);
    if (auto* p = m_profileMgr->activeProfile()) m_rightPane->refreshPlugins(p);
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    updatePluginNotice();
    statusBar()->showMessage(
        QString("Created mod '%1' from Overwrite (%2 files)").arg(name).arg(moved));
}

void MainWindow::onClearShaderCache(const QString& modId) {
    Q_UNUSED(modId); // the action is anchored to the CS mod but clears global state
    auto reply = QMessageBox::question(this, "Clear Shader Cache",
        "Clear the Community Shaders shader cache? CS recompiles it on next launch "
        "- the first load will be slower.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    // Resolve the managed cache's staging root (empty if not managed -> that copy is
    // skipped, but the live game-dir + Overwrite copies are still cleared).
    auto* scP = m_profileMgr->activeProfile();
    const QString cacheStaging = cacheStagingPath(scP);

    // Name the CS version in the status so it's clear WHICH version's cache cleared
    // (only the active version's staged folder is touched).
    QString versionSuffix;
    if (scP) {
        const solero::ModEntry* cs = scP->modList().findCommunityShaders();
        if (cs && !cs->version.isEmpty())
            versionSuffix = " (" + cs->version + ")";
    }

    const auto result = solero::clearShaderCache(
        solero::AppConfig::instance().gameDir(),
        solero::AppConfig::overwriteDir(scP ? scP->name() : QString()),
        cacheStaging);

    m_modListView->invalidateModCache(); // staged/overwrite contents changed
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }

    if (result.removedPaths.isEmpty()) {
        statusBar()->showMessage(
            "Shader cache" + versionSuffix + " already clear - nothing to remove.");
    } else {
        const double mb = result.bytesRemoved / (1024.0 * 1024.0);
        statusBar()->showMessage(
            QString("Cleared shader cache%1 (%2 location(s), %3 MB freed).")
                .arg(versionSuffix).arg(result.removedPaths.size()).arg(mb, 0, 'f', 1));
    }
}

void MainWindow::maybeOfferShaderCacheManagement() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    if (!profile->modList().findCommunityShaders()) return; // CS not installed
    if (profile->shaderCache().managed) return;              // already managed
    if (solero::AppConfig::instance().shaderCacheDeclined()) return; // declined before

    auto reply = QMessageBox::question(this, "Community Shaders",
        "Community Shaders detected. Let Solero manage its shader cache? It stores "
        "the compiled shaders in a hidden managed mod, captures them after you play, "
        "and adds a right-click 'Clear Shader Cache' on the mod.\n\n"
        "Choose No to leave the cache in the game folder as it is now.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (reply == QMessageBox::Yes) {
        enableManagedCache();
    } else {
        solero::AppConfig::instance().setShaderCacheDeclined(true);
        solero::AppConfig::instance().save();
    }
}

void MainWindow::enableManagedCache() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    if (profile->shaderCache().active()) return; // idempotent

    const QString folder = solero::uniqueStagingFolder(
        solero::sanitizeStagingFolder("Community Shaders - Shader Cache"),
        takenStagingFolders(profile));
    profile->shaderCache().managed = true;
    profile->shaderCache().folders.insert(solero::activeCacheKey(profile->modList()), folder);

    // Create the staging dir with an empty Data/ShaderCache so captures land there.
    const QString modRoot = solero::AppConfig::instance().stagingDir() + "/" + folder;
    QDir().mkpath(modRoot + "/Data/ShaderCache");

    profile->save();
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    statusBar()->showMessage("Solero is now managing the Community Shaders shader cache.");
}

void MainWindow::onRedownloadMod(const QString& modId) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    solero::ModEntry* mod = profile->modList().findById(modId);
    if (!mod) return;
    // If the mod's Nexus ids are unknown (e.g. imported mods), identify it by
    // MD5 first. Bails with a message if that isn't possible.
    if (!ensureNexusIds(mod)) return;
    // ensureNexusIds refreshes the list view; re-fetch to avoid a stale pointer.
    mod = profile->modList().findById(modId);
    if (!mod) return;

    const QString nexusModId = mod->nexusModId;
    const QString fileId     = mod->nexusFileId;
    const QString name       = mod->name;

    const QString url = solero::NexusApi::downloadUrl(nexusModId, fileId);
    if (url.isEmpty()) {
        if (!solero::NexusApi::keyAvailable())
            requireNexusKey("download from within Solero");
        else
            QMessageBox::information(this, "Redownload from Nexus",
                "This download is unavailable or requires Nexus Premium. Use the "
                "mod page's 'Mod Manager Download' (nxm) button on the website instead.");
        return;
    }

    // Pick a clean filename + current version from the Nexus file list (same source
    // the update flow uses); fall back to the URL/synthetic name.
    QString fileName, version = mod->version;
    for (const auto& f : solero::NexusApi::files(nexusModId)) {
        if (f.fileId == fileId) {
            fileName = f.name;
            if (!f.version.isEmpty()) version = f.version;
            break;
        }
    }
    if (fileName.isEmpty()) fileName = QUrl(url).fileName();
    if (fileName.isEmpty()) fileName = "nexus-" + nexusModId + "-" + fileId + ".archive";

    // Tag the download so its sidecar is written on finish (matches onNexusDownload).
    m_nxmMeta[fileName] = QJsonObject{
        {"game", "skyrimspecialedition"}, {"modId", nexusModId},
        {"fileId", fileId}, {"version", version}};
    m_downloads->enqueue(url, fileName, solero::AppConfig::instance().downloadsDir());
    m_rightPane->showDownloadsTab();
    statusBar()->showMessage("Redownloading " + name + "\xe2\x80\xa6");
}

void MainWindow::onRetryDownload(const QString& fileName) {
    // Find the retained failure context.
    int idx = -1;
    for (int i = 0; i < m_failedDownloads.size(); ++i)
        if (m_failedDownloads[i].fileName == fileName) { idx = i; break; }
    if (idx < 0) return;
    const FailedDownload fd = m_failedDownloads.at(idx);

    QString url;
    const QString modId  = fd.meta.value("modId").toString();
    const QString fileId = fd.meta.value("fileId").toString();
    const QString game   = fd.meta.value("game").toString(solero::NexusApi::kDefaultGame);
    if (!modId.isEmpty() && !fileId.isEmpty()) {
        // Nexus download: re-resolve a fresh CDN URL (resolved URLs expire).
        url = solero::NexusApi::downloadUrl(modId, fileId, game);
        if (url.isEmpty()) {
            if (!solero::NexusApi::keyAvailable())
                requireNexusKey("download from within Solero");
            else
                QMessageBox::information(this, "Retry download",
                    "This download is unavailable or requires Nexus Premium. Use the "
                    "mod page's 'Mod Manager Download' (nxm) button on the website instead.");
            return; // keep the failed row so the user can try the website route
        }
    } else {
        // Only Nexus downloads (which carry mod/file ids) can be re-resolved.
        QMessageBox::information(this, "Retry download",
            "Solero can only retry Nexus downloads automatically. Start this one "
            "again from the mod page or tool.");
        return;
    }

    // Remove the failed entry and re-tag metadata so the sidecar is written on success.
    m_failedDownloads.removeAt(idx);
    if (!fd.meta.isEmpty()) m_nxmMeta[fileName] = fd.meta;
    QList<QPair<QString,QString>> failPairs;
    for (const FailedDownload& d : m_failedDownloads)
        failPairs.append({d.fileName, d.error});
    m_rightPane->downloadsTab()->setFailedDownloads(failPairs);

    m_downloads->enqueue(url, fileName, solero::AppConfig::instance().downloadsDir());
    m_rightPane->showDownloadsTab();
    statusBar()->showMessage("Retrying " + fileName + "\xe2\x80\xa6");
}

void MainWindow::openSettingsDialog() {
    solero::SettingsDialog dlg(this);
    // "Connect to Nexus": switch the central view to the embedded browser and
    // jump straight to the personal API-key page (login cookie persists there).
    connect(&dlg, &solero::SettingsDialog::connectNexusRequested, this, [this]{
        if (m_browseAction) m_browseAction->setChecked(true);
        if (m_nexusWeb) m_nexusWeb->openUrl(solero::NexusWebView::apiKeyUrl());
    });
    // SKSE section: show the version installed for the active profile, and install a
    // specific build when the user picks one.
    dlg.setSkseInstalledVersion(installedSkseVersion(m_profileMgr->activeProfile()));
    connect(&dlg, &solero::SettingsDialog::skseInstallRequested, this,
            [this](const QString& fileId, const QString& version) {
                installSkseVersion(fileId, version);
            });
    if (dlg.exec() == QDialog::Accepted)
        statusBar()->showMessage("Settings updated.");
}

bool MainWindow::requireNexusKey(const QString& context) {
    if (solero::NexusApi::keyAvailable()) return true;
    QMessageBox box(QMessageBox::Information, "Nexus Account",
        QString("A Nexus account is required to %1.\n\n"
                "Connect your Nexus account in Settings \xe2\x80\xba Nexus Account.")
            .arg(context.isEmpty() ? QStringLiteral("use this feature") : context),
        QMessageBox::Close, this);
    QPushButton* openBtn = box.addButton("Open Settings\xe2\x80\xa6", QMessageBox::AcceptRole);
    box.setDefaultButton(openBtn);
    box.exec();
    if (box.clickedButton() == openBtn) openSettingsDialog();
    // Re-check: the user may have connected their account from Settings.
    return solero::NexusApi::keyAvailable();
}

void MainWindow::onEndorseMod(const QString& modId) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    solero::ModEntry* mod = profile->modList().findById(modId);
    if (!mod || mod->nexusModId.isEmpty()) return;

    if (!requireNexusKey("endorse mods")) return;

    QString version = mod->version;
    if (version.isEmpty())
        version = solero::NexusApi::modInfo(mod->nexusModId).version;

    auto res = solero::NexusApi::endorse(mod->nexusModId, version, false);
    if (res.ok)
        QMessageBox::information(this, "Endorse on Nexus",
            "Thanks - endorsed " + mod->name + "!");
    else
        QMessageBox::warning(this, "Endorse on Nexus",
            res.message.isEmpty() ? QString("The endorsement could not be submitted - Nexus did not return a result. Try again from the mod page.") : res.message);
}

void MainWindow::onTrackMod(const QString& modId) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    solero::ModEntry* mod = profile->modList().findById(modId);
    if (!mod || mod->nexusModId.isEmpty()) return;

    if (!requireNexusKey("track mods")) return;

    auto res = solero::NexusApi::track(mod->nexusModId);
    if (res.ok)
        statusBar()->showMessage("Tracking " + mod->name + " on Nexus.");
    else
        statusBar()->showMessage(res.message.isEmpty()
            ? QString("Couldn't track %1 on Nexus.").arg(mod->name) : res.message);
}

void MainWindow::onUntrackMod(const QString& modId) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    solero::ModEntry* mod = profile->modList().findById(modId);
    if (!mod || mod->nexusModId.isEmpty()) return;

    if (!requireNexusKey("untrack mods")) return;

    auto res = solero::NexusApi::untrack(mod->nexusModId);
    if (res.ok)
        statusBar()->showMessage("Stopped tracking " + mod->name + " on Nexus.");
    else
        statusBar()->showMessage(res.message.isEmpty()
            ? QString("Couldn't untrack %1 on Nexus.").arg(mod->name) : res.message);
}

void MainWindow::onViewNexusPage(const QString& modId) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    solero::ModEntry* mod = profile->modList().findById(modId);
    if (!mod || mod->nexusModId.isEmpty()) return; // not a Nexus mod: nothing to open

    const QString url = solero::NexusApi::modPageUrl(mod->nexusModId);
    if (m_browseAction) m_browseAction->setChecked(true);
    if (m_nexusWeb) m_nexusWeb->openUrl(QUrl(url));
}

void MainWindow::onUpdateMod(const QString& modId) {
    if (!requireNexusKey("download updates")) return;
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    solero::ModEntry* mod = profile->modList().findById(modId);
    if (!mod || mod->nexusModId.isEmpty()) return;

    // Single-flight end-to-end (resolve -> download -> reinstall). Guarding on
    // both the resolve watcher and any in-flight pending download removes the
    // m_pendingUpdates filename-key collision and the m_updateTargetId/Name
    // clobber that two overlapping updates would otherwise cause.
    if (m_updateResolveWatcher.isRunning() || !m_pendingUpdates.isEmpty()) {
        statusBar()->showMessage("An update is already in progress\xe2\x80\xa6");
        return;
    }

    const QString nexusModId = mod->nexusModId;
    m_updateTargetId   = mod->id;
    m_updateTargetName = mod->name;

    statusBar()->showMessage("Resolving latest version for " + mod->name + "\xe2\x80\xa6");

    m_updateResolveWatcher.setFuture(QtConcurrent::run([nexusModId]() -> ResolvedUpdate {
        ResolvedUpdate out;
        const auto files = solero::NexusApi::files(nexusModId);
        if (files.isEmpty()) { out.error = "Could not list files for this mod."; return out; }

        const QString latest = solero::NexusApi::latestVersion(nexusModId);

        // Pick the best file: prefer main-category files, choosing the highest
        // version among them; otherwise the file whose version == latestVersion;
        // else fall back to the last (roughly newest) file in the list.
        const solero::NexusApi::NexusFile* picked = nullptr;
        for (const auto& f : files) {
            if (f.category.compare("MAIN", Qt::CaseInsensitive) != 0) continue;
            if (!picked || f.version > picked->version) picked = &f;
        }
        if (!picked && !latest.isEmpty()) {
            for (const auto& f : files)
                if (f.version == latest) { picked = &f; break; }
        }
        if (!picked) picked = &files.last();

        out.fileId   = picked->fileId;
        out.fileName = picked->name;
        out.version  = picked->version;
        out.url      = solero::NexusApi::downloadUrl(nexusModId, picked->fileId);
        if (out.url.isEmpty()) {
            out.error = "Could not get a download link (Premium required, or file unavailable).";
            return out;
        }
        out.ok = true;
        return out;
    }));
}

bool MainWindow::skseInstalledFor(solero::Profile* profile) const {
    // 1) The game dir root (a manual/Steam SKSE install drops the loader here).
    const QString gameDir = solero::AppConfig::instance().gameDir();
    if (!gameDir.isEmpty()) {
        const QString loader = gameDir + "/skse64_loader.exe";
        if (QFileInfo::exists(loader)) return true;
        // Case-insensitive fallback for the bare filename in the game root.
        for (const QString& f : QDir(gameDir).entryList(QDir::Files))
            if (f.compare("skse64_loader.exe", Qt::CaseInsensitive) == 0) return true;
    }
    // 2) Any enabled mod's staging dir (recursively; mods are symlinked in).
    if (profile) {
        const QString stagingDir = solero::AppConfig::instance().stagingDir();
        for (const solero::ModEntry& m : profile->modList()) {
            if (m.type != solero::EntryType::Mod || !m.enabled) continue;
            const QString modDir = solero::stagingPathFor(stagingDir, m);
            if (!QDir(modDir).exists()) continue;
            QDirIterator it(modDir, QDir::Files,
                            QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
            while (it.hasNext()) {
                it.next();
                if (it.fileName().compare("skse64_loader.exe", Qt::CaseInsensitive) == 0)
                    return true; // stop early on first hit
            }
        }
    }
    return false;
}

void MainWindow::ensureSkse() {
    // Every profile should have SKSE; install it silently when missing rather than
    // prompting. If there's no Nexus key (or no network), defer quietly - the call
    // fires again on the next launch / profile switch, so it self-heals later.
    if (m_skseInstalling) return;                  // already downloading SKSE
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    if (skseInstalledFor(profile)) return;
    if (!solero::NexusApi::keyAvailable()) return; // defer: retry on next launch/switch

    statusBar()->showMessage("Setting up SKSE\xe2\x80\xa6");
    installSkseFromNexus();
}

void MainWindow::installSkseFromNexus() {
    if (m_skseInstalling || m_skseResolveWatcher.isRunning()) {
        statusBar()->showMessage("SKSE install already in progress\xe2\x80\xa6");
        return;
    }
    m_skseInstalling = true;
    statusBar()->showMessage("Resolving SKSE download\xe2\x80\xa6");

    m_skseResolveWatcher.setFuture(QtConcurrent::run([]() -> ResolvedSkse {
        ResolvedSkse out;
        const QString game  = "skyrimspecialedition";
        const QString modId = "30379";
        const auto files = solero::NexusApi::files(modId, game);

        QString fileId, version;
        if (!files.isEmpty()) {
            // Prefer the main-category file, choosing the highest version.
            const solero::NexusApi::NexusFile* picked = nullptr;
            for (const auto& f : files) {
                if (f.category.compare("MAIN", Qt::CaseInsensitive) != 0) continue;
                if (!picked || f.version > picked->version) picked = &f;
            }
            if (!picked) {
                // Fall back to the highest-version file overall.
                for (const auto& f : files)
                    if (!picked || f.version > picked->version) picked = &f;
            }
            if (picked) { fileId = picked->fileId; version = picked->version; }
        }
        // Last-resort fallback to the known-good current main file (v2.2.6).
        if (fileId.isEmpty()) { fileId = "462377"; version = "2.2.6"; }

        out.fileId   = fileId;
        out.version  = version;
        out.url      = solero::NexusApi::downloadUrl(modId, fileId, game);
        if (out.url.isEmpty()) {
            out.error = "Could not get a download link for SKSE "
                        "(Premium required, or file unavailable).";
            return out;
        }
        // Derive a sensible saved filename from the URL (before any query string).
        QString fn = QUrl(out.url).fileName();
        if (fn.isEmpty()) fn = "skse64_" + version + ".7z";
        out.fileName = fn;
        out.ok = true;
        return out;
    }));
}

QString MainWindow::installedSkseVersion(solero::Profile* profile) const {
    if (!profile) return {};
    if (const auto* e = profile->modList().findByNexusId("30379"))
        return e->version;
    return {};
}

void MainWindow::installSkseVersion(const QString& fileId, const QString& version) {
    if (m_skseInstalling || m_skseResolveWatcher.isRunning()) {
        statusBar()->showMessage("SKSE install already in progress\xe2\x80\xa6");
        return;
    }
    if (fileId.isEmpty()) return;
    const QString game = "skyrimspecialedition";
    const QString modId = "30379";

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString url = solero::NexusApi::downloadUrl(modId, fileId, game);
    QApplication::restoreOverrideCursor();
    if (url.isEmpty()) {
        QMessageBox::warning(this, "Install SKSE",
            "Couldn't get a download link for that SKSE build "
            "(Nexus Premium may be required).");
        return;
    }
    QString fn = QUrl(url).fileName();
    if (fn.isEmpty()) fn = "skse64_" + version + ".7z";

    // Tag with SKSE's Nexus identity so the auto-install path replaces any existing
    // SKSE mod in place (same modId + name -> Replace) rather than adding a duplicate.
    m_skseInstalling = true;
    m_nxmMeta[fn] = QJsonObject{
        {"game", game}, {"modId", modId}, {"fileId", fileId},
        {"version", version}, {"name", "Skyrim Script Extender (SKSE64)"}};
    m_autoInstall.insert(fn);
    m_downloads->enqueue(url, fn, solero::AppConfig::instance().downloadsDir());
    m_rightPane->showDownloadsTab();
    statusBar()->showMessage("Downloading SKSE " + version + "\xe2\x80\xa6");
}

void MainWindow::markRequirementResult(const QString& reqModId, bool installed) {
    // Notify every open RequirementsDialog (modal dialogs can stack via nested
    // exec()). QPointers auto-null when a dialog closes; prune them as we go.
    for (int i = m_openReqDialogs.size() - 1; i >= 0; --i) {
        auto& d = m_openReqDialogs[i];
        if (!d) { m_openReqDialogs.removeAt(i); continue; }
        if (installed) d->markInstalled(reqModId);
        else           d->markFailed(reqModId);
    }
}

void MainWindow::checkRequirementsAfterInstall(solero::Profile* profile,
                                               const QString& dependentModId,
                                               const QString& nexusModId,
                                               const QString& game) {
    if (!profile || nexusModId.isEmpty()) return;
    if (!solero::NexusApi::keyAvailable()) return; // can't query or install without a key

    const QString g = game.isEmpty() ? QString(solero::NexusApi::kDefaultGame) : game;
    QList<solero::NexusApi::ModRequirement> reqs;
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        reqs = solero::NexusApi::modRequirements(nexusModId, g);
        QApplication::restoreOverrideCursor();
    }
    if (reqs.isEmpty()) return;

    // Skyrim VR vs SE: when the install is the flatscreen SE build (no SkyrimVR.exe),
    // VR-specific requirements (VR ports / VRIK / "for VR only" patches) don't apply,
    // so they're suppressed below.
    const QString gameDir = solero::AppConfig::instance().gameDir();
    const bool isVR = QFile::exists(gameDir + "/SkyrimVR.exe");
    auto looksVrSpecific = [](const QString& name, const QString& notes) {
        // Standalone "VR" token, "SkyrimVR", or "VRIK" in the name/notes. \bvr\b
        // catches "VR", "(VR)", "for VR", "VR only" without matching words like
        // "Verdant" or "improved".
        static const QRegularExpression re(
            QStringLiteral(R"(\bvr\b|skyrimvr|\bvrik\b)"),
            QRegularExpression::CaseInsensitiveOption);
        return re.match(name).hasMatch() || re.match(notes).hasMatch();
    };

    // Keep only requirements not already satisfied in this profile.
    QList<solero::RequirementsDialog::Item> missing;
    const auto& ml = profile->modList();
    for (const auto& r : reqs) {
        // Already present as an enabled mod carrying this Nexus id?
        bool have = false;
        for (const auto& e : ml.entries()) {
            if (e.type != solero::EntryType::Mod || !e.enabled) continue;
            if (e.nexusModId == r.modId) { have = true; break; }
        }
        if (have) continue;
        // SKSE: Solero always installs the Script Extender per profile (ensureSkse),
        // so never warn about it as a missing requirement - neither its Nexus page
        // (30379) nor the off-site skse.silverlock.org entry. Matched by modId/URL
        // only (not name) so "Address Library for SKSE Plugins" etc. still surface.
        if (r.modId == "30379"
            || r.url.contains(QStringLiteral("silverlock.org"), Qt::CaseInsensitive))
            continue;
        // Not on Skyrim VR -> don't list VR-specific requirements.
        if (!isVR && looksVrSpecific(r.modName, r.notes)) continue;
        missing.append({r.modId, r.modName, r.notes, r.url, r.external});
    }
    if (missing.isEmpty()) return;

    const solero::ModEntry* dep = ml.findById(dependentModId);
    const QString depName = dep ? dep->name : QStringLiteral("this mod");

    auto* dlg = new solero::RequirementsDialog(depName, missing, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &solero::RequirementsDialog::installRequested, this,
            [this, dependentModId, g](const QString& modId, const QString& modName) {
                downloadRequirement(modId, modName, dependentModId, g);
            });
    // Track the dialog so a requirement's async install can flip its row to
    // "Installed"/"Retry" when it finishes (the QPointer auto-nulls on close).
    m_openReqDialogs.append(dlg);
    dlg->exec();
}

void MainWindow::downloadRequirement(const QString& reqModId, const QString& reqName,
                                     const QString& dependentModId, const QString& game) {
    const QString g = game.isEmpty() ? QString(solero::NexusApi::kDefaultGame) : game;

    // Pick the requirement's main file at the highest version (fallback: highest overall).
    QString fileId, version;
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const auto files = solero::NexusApi::files(reqModId, g);
        QApplication::restoreOverrideCursor();
        const solero::NexusApi::NexusFile* picked = nullptr;
        for (const auto& f : files) {
            if (f.category.compare("MAIN", Qt::CaseInsensitive) != 0) continue;
            if (!picked || f.version > picked->version) picked = &f;
        }
        if (!picked)
            for (const auto& f : files)
                if (!picked || f.version > picked->version) picked = &f;
        if (picked) { fileId = picked->fileId; version = picked->version; }
    }
    if (fileId.isEmpty()) {
        QMessageBox::warning(this, "Install requirement",
            QString("Couldn't find a downloadable file for \"%1\" on Nexus.").arg(reqName));
        markRequirementResult(reqModId, /*installed=*/false); // reset the stuck "Installing…"
        return;
    }

    const QString url = solero::NexusApi::downloadUrl(reqModId, fileId, g);
    if (url.isEmpty()) {
        QMessageBox::information(this, "Install requirement",
            QString("In-app download of \"%1\" needs Nexus Premium. Use the mod page's "
                    "'Mod Manager Download' (nxm) button on the website instead.").arg(reqName));
        markRequirementResult(reqModId, /*installed=*/false); // reset the stuck "Installing…"
        return;
    }
    QString fn = QUrl(url).fileName();
    if (fn.isEmpty()) fn = "nexus-" + reqModId + "-" + fileId + ".archive";

    // Tag for sidecar + auto-install on finish, and record where to place it.
    m_nxmMeta[fn] = QJsonObject{
        {"game", g}, {"modId", reqModId}, {"fileId", fileId},
        {"version", version}, {"name", reqName}};
    m_autoInstall.insert(fn);
    m_placeAboveByModId[reqModId] = dependentModId;
    m_downloads->enqueue(url, fn, solero::AppConfig::instance().downloadsDir());
    m_rightPane->showDownloadsTab();
    statusBar()->showMessage("Downloading " + reqName + "\xe2\x80\xa6");
}

void MainWindow::onCheckUpdates() {
    runUpdateCheck(/*silentIfNone=*/false);
}

void MainWindow::runUpdateCheck(bool silentIfNone) {
    // Never start a second check while one is in flight.
    if (m_updateWatcher.isRunning()) {
        if (!silentIfNone)
            statusBar()->showMessage("An update check is already running\xe2\x80\xa6");
        return;
    }

    if (!solero::NexusApi::keyAvailable()) {
        // Silent auto-check must never pop a modal; only the explicit check does.
        if (!silentIfNone) requireNexusKey("check for updates");
        return;
    }
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;

    // Collect mods with Nexus metadata. fileId enables an ACCURATE check (is there a
    // newer main file than the one installed?); archive enables MD5 back-fill of a
    // missing fileId. version is the installed file's version.
    struct Item { QString id, modId, version, fileId, archive; };
    QList<Item> items;
    const auto& list = profile->modList();
    for (int i = 0; i < list.count(); ++i) {
        const auto& e = list.at(i);
        if (e.type != solero::EntryType::Mod || e.nexusModId.isEmpty()) continue;
        items.append({e.id, e.nexusModId, e.version, e.nexusFileId, e.sourceArchive});
    }
    if (items.isEmpty()) {
        if (!silentIfNone)
            QMessageBox::information(this, "Check for Mod Updates",
                "No mods have Nexus metadata yet - install via the 'Mod Manager "
                "Download' button or use right-click \xe2\x86\x92 Identify on Nexus.");
        return;
    }

    // Disable the menu action so the check can't be double-run. Progress reports
    // inline on the Mod Info header row (not the status bar) while the network
    // calls fan out on a bounded background pool.
    if (m_checkUpdatesAction) m_checkUpdatesAction->setEnabled(false);

    // Only the EXPLICIT check (not the silent on-open one) does MD5 back-fill, so a
    // launch never spends time hashing archives.
    const bool doBackfill = !silentIfNone;

    // The v1 files.json endpoint is per-mod, so mods sharing a Nexus modId
    // (variants/submods) need only one network call: dedupe up front and fetch
    // each distinct modId once. Progress counts distinct modIds.
    QStringList modIds;
    {
        QSet<QString> seen;
        for (const auto& it : items)
            if (!seen.contains(it.modId)) { seen.insert(it.modId); modIds.append(it.modId); }
    }

    // Generation stamp for this check. Profile switches bump m_updateGen, so a
    // check that outlives its profile is discarded on finish and its progress
    // callbacks go quiet - no cross-profile flag bleed, no touching stale state.
    const quint64 gen = ++m_updateGen;

    // QPointer so a queued progress callback arriving during teardown can never
    // touch a dead widget; if the panel is gone, so is this window.
    QPointer<solero::BottomPanel> panel(m_bottomPanel);
    const int total = modIds.size();
    if (panel) panel->setUpdateProgress(0, total);

    auto future = QtConcurrent::run([this, items, modIds, doBackfill, gen, panel, total]() -> UpdateScan {
        UpdateScan scan;
        scan.gen = gen;

        // Bounded fan-out: each NexusApi call blocks on a curl QProcess, so run up
        // to 6 at once on a LOCAL pool (never starve the shared global pool). With
        // ~450 mods this turns minutes of serial round-trips into total/6 batches.
        QHash<QString, QList<solero::NexusApi::NexusFile>> filesByMod;
        filesByMod.reserve(total);
        QMutex mutex;
        std::atomic<int> done{0};
        {
            QThreadPool pool;
            pool.setMaxThreadCount(6);
            for (const QString& modId : modIds) {
                pool.start([&, modId]() {
                    const auto files = solero::NexusApi::files(modId);
                    {
                        QMutexLocker lock(&mutex);
                        filesByMod.insert(modId, files);
                    }
                    const int d = ++done;
                    // Marshal progress to the UI thread. The lambda runs there, so
                    // the QPointer and generation checks are race-free: a stale
                    // generation (profile switched) or dead panel goes silent.
                    QMetaObject::invokeMethod(qApp, [this, panel, d, total, gen]() {
                        if (!panel) return;            // window tearing down
                        if (gen != m_updateGen) return; // superseded check
                        panel->setUpdateProgress(d, total);
                    }, Qt::QueuedConnection);
                });
            }
            pool.waitForDone();
        }

        int backfilled = 0;
        // The explicit check back-fills every identifiable mod (so one run covers the
        // whole list); the silent on-open check never hashes (cap 0) to keep launches fast.
        const int kBackfillCap = doBackfill ? items.size() : 0;
        for (const auto& it : items) {
            QString fileId = it.fileId, installedVer = it.version;

            // A mod with no fileId can't be checked accurately. Identify it by MD5 so
            // coverage grows; never flag on a guessed version.
            if (fileId.isEmpty()) {
                if (!doBackfill || backfilled >= kBackfillCap
                    || it.archive.isEmpty() || !QFile::exists(it.archive))
                    continue;
                QFile f(it.archive);
                if (!f.open(QIODevice::ReadOnly)) continue;
                QCryptographicHash hash(QCryptographicHash::Md5);
                constexpr qint64 kChunk = 1 << 20;
                while (!f.atEnd()) {
                    const QByteArray c = f.read(kChunk);
                    if (c.isEmpty()) break;
                    hash.addData(c);
                }
                f.close();
                ++backfilled;
                const auto m = solero::NexusApi::md5Search(QString::fromLatin1(hash.result().toHex()));
                if (!m.ok || m.fileId.isEmpty()) continue;
                fileId = m.fileId;
                if (!m.version.isEmpty()) installedVer = m.version;
                scan.backfill.insert(it.id, qMakePair(fileId, installedVer));
            }

            // Accurate check: find the newest main file and flag only when it is a
            // DIFFERENT, newer-versioned file than the one installed - so having the
            // current file never false-flags. (Files were pre-fetched above.)
            const auto files = filesByMod.value(it.modId);
            const solero::NexusApi::NexusFile* latestMain = nullptr;
            for (const auto& f : files) {
                if (f.category.compare("MAIN", Qt::CaseInsensitive) != 0) continue;
                if (!latestMain || isVersionNewer(latestMain->version, f.version)) latestMain = &f;
            }
            if (!latestMain || latestMain->fileId == fileId) continue;
            if (!installedVer.isEmpty() && !latestMain->version.isEmpty()
                && isVersionNewer(installedVer, latestMain->version))
                scan.updates.insert(it.id, qMakePair(normalizeVersion(installedVer),
                                                     normalizeVersion(latestMain->version)));
        }
        return scan;
    });
    m_updateWatcher.setFuture(future);
}

void MainWindow::maybeAutoCheckUpdates() {
    auto& cfg = solero::AppConfig::instance();
    if (!cfg.autoCheckUpdates()) return;
    if (m_updateWatcher.isRunning()) return;
    if (!solero::NexusApi::keyAvailable()) return;
    // Always run one check at app launch (switchProfile runs once at startup for
    // the initial profile); later mid-session profile switches stay throttled.
    if (!m_didLaunchUpdateCheck) {
        m_didLaunchUpdateCheck = true;
    } else {
        // Throttle: only auto-check if it's been more than 6 hours since the last one.
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (now - cfg.lastUpdateCheckEpoch() <= 6 * 3600) return;
    }
    runUpdateCheck(/*silentIfNone=*/true);
}

void MainWindow::saveUpdateFlags(const QHash<QString, QPair<QString,QString>>& updates) const {
    auto* profile = m_profileMgr ? m_profileMgr->activeProfile() : nullptr;
    if (!profile) return;
    QJsonObject obj; // local mod id -> [installed, latest]
    for (auto it = updates.constBegin(); it != updates.constEnd(); ++it)
        obj[it.key()] = QJsonArray{ it.value().first, it.value().second };
    QFile f(profile->path() + "/updates.json");
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QHash<QString, QPair<QString,QString>> MainWindow::loadUpdateFlags() const {
    QHash<QString, QPair<QString,QString>> out;
    auto* profile = m_profileMgr ? m_profileMgr->activeProfile() : nullptr;
    if (!profile) return out;
    QFile f(profile->path() + "/updates.json");
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonArray a = it.value().toArray();
        if (a.size() == 2) out.insert(it.key(), qMakePair(a[0].toString(), a[1].toString()));
    }
    return out;
}

bool MainWindow::ensureNexusIds(solero::ModEntry* mod) {
    if (!mod) return false;
    if (!mod->nexusFileId.isEmpty() && !mod->nexusModId.isEmpty())
        return true; // already known

    if (!requireNexusKey("identify mods")) return false;

    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return false;

    // Nexus md5_search matches the uploaded ARCHIVE's md5, so we need the original
    // source archive. Imported mods without one can't be matched.
    if (mod->sourceArchive.isEmpty() || !QFile::exists(mod->sourceArchive)) {
        QMessageBox::information(this, "Redownload from Nexus",
            "Re-downloading needs to identify this mod on Nexus first, which requires "
            "the original archive it was installed from.\nThis mod has no source "
            "archive on disk, so it can't be matched.");
        return false;
    }

    QString md5;
    {
        solero::ProgressModal prog(this, "Redownload from Nexus", "Hashing\xe2\x80\xa6");
        prog.pump();
        QFile f(mod->sourceArchive);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, "Redownload from Nexus",
                "Could not read the source archive for this mod. The file may have been moved or deleted from the downloads folder.");
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Md5);
        constexpr qint64 kChunk = 1 << 20; // 1 MiB
        while (!f.atEnd()) {
            QByteArray chunk = f.read(kChunk);
            if (chunk.isEmpty()) break;
            hash.addData(chunk);
            prog.pump();
        }
        md5 = QString::fromLatin1(hash.result().toHex());
    }

    auto m = solero::NexusApi::md5Search(md5);
    if (!m.ok) {
        QMessageBox::information(this, "Redownload from Nexus",
            "No Nexus match found for this archive, so it can't be re-downloaded "
            "automatically.");
        return false;
    }

    mod->nexusModId = m.modId;
    mod->nexusFileId = m.fileId;
    if (!m.version.isEmpty()) mod->version = m.version;
    profile->save();
    m_modListView->setProfile(profile);
    return true;
}

void MainWindow::refreshDeployState() {
    // A deployment persists on disk via the deploy record in the game dir.
    // Detect it on startup so the toggle reflects reality across relaunches.
    m_deployed = false;
    if (solero::AppConfig::instance().isConfigured()) {
        QString rec = solero::DeployEngine::recordPath(
            solero::AppConfig::instance().gameDir());
        m_deployed = QFile::exists(rec);
    }
    m_deployDirty = false;
    m_loadOrderDirty = false;
    updateDeployButton();
    updatePluginNotice();
    updateSortButton();
    refreshHealthIndicator(); // m_deployed just settled - reflect it in the count
}

void MainWindow::onNewProfile() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Profile", "Profile name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();
    if (!m_profileMgr->createProfile(name)) {
        QMessageBox::warning(this, "New Profile", QString("A profile named '%1' already exists. Choose a different name.").arg(name));
        return;
    }
    // refreshProfileCombo blocks combo signals while it repopulates, so merely
    // listing the new profile doesn't trigger a switch - promptSwitchToNewProfile
    // owns that decision.
    refreshProfileCombo();
    promptSwitchToNewProfile(name);
}

void MainWindow::onDeleteProfile() {
    QString current = m_profileCombo->currentText();
    if (current.isEmpty()) return;
    if (m_profileMgr->profileNames().count() <= 1) {
        QMessageBox::warning(this, "Delete Profile", "You can't delete the only profile. Create another profile first.");
        return;
    }
    QMessageBox confirm(QMessageBox::Question, "Delete Profile",
        QString("Delete profile '%1'? This cannot be undone.").arg(current),
        QMessageBox::Cancel, this);
    auto* deleteBtn = confirm.addButton("Delete", QMessageBox::DestructiveRole);
    confirm.setDefaultButton(QMessageBox::Cancel); // Enter must not delete
    confirm.exec();
    if (confirm.clickedButton() != deleteBtn) return;
    m_profileMgr->deleteProfile(current);
    refreshProfileCombo();
    switchProfile(m_profileMgr->profileNames().first());
}

void MainWindow::onRenameProfile() {
    const QString current = m_profileCombo->currentText();
    if (current.isEmpty()) return;
    bool ok;
    QString name = QInputDialog::getText(this, "Rename Profile", "New profile name:",
                                         QLineEdit::Normal, current, &ok);
    if (!ok) return;
    name = name.trimmed();
    if (name.isEmpty() || name == current) return;

    if (!m_profileMgr->renameProfile(current, name)) {
        QMessageBox::warning(this, "Rename Profile",
            QString("Couldn't rename to '%1'. The name may be invalid or already in use.").arg(name));
        return;
    }

    // Move the per-profile Overwrite capture dir too, so captured files aren't orphaned.
    const QString oldOverwrite = solero::AppConfig::overwriteDir(current);
    const QString newOverwrite = solero::AppConfig::overwriteDir(name);
    if (QDir(oldOverwrite).exists() && !QDir(newOverwrite).exists())
        QDir().rename(oldOverwrite, newOverwrite);

    // Rebuild the combo (signals blocked inside refreshProfileCombo) and select the
    // new name; setCurrentText emits currentTextChanged -> switchProfile(name),
    // which reloads the profile and persists it as lastProfile.
    refreshProfileCombo();
    m_profileCombo->setCurrentText(name);
    statusBar()->showMessage(QString("Renamed profile to '%1'.").arg(name));
}

void MainWindow::onImportMo2() {
    if (!solero::AppConfig::instance().isConfigured()) {
        statusBar()->showMessage("Configure the game first (Game Settings...)."); return;
    }
    QString modlist = QFileDialog::getOpenFileName(
        this, "Select MO2 profile's modlist.txt", QDir::homePath(), "modlist.txt (modlist.txt);;All files (*)");
    if (modlist.isEmpty()) return;
    QString profileDir = QFileInfo(modlist).path();
    // mods dir: MO2 layout is <base>/mods and <base>/profiles/<name>/. Go up two from profileDir.
    QString modsDir = QDir(profileDir + "/../../mods").canonicalPath();
    if (modsDir.isEmpty() || !QDir(modsDir).exists()) {
        modsDir = QFileDialog::getExistingDirectory(this, "Select MO2 'mods' folder", QDir::homePath());
        if (modsDir.isEmpty()) return;
    }
    bool ok;
    QString name = QInputDialog::getText(this, "Import MO2 Profile", "New Solero profile name:",
                                         QLineEdit::Normal, "Imported", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto ret = QMessageBox::question(this, "Import method",
        "Symlink mod folders (fast, shares files with MO2) instead of copying?\n"
        "Yes = symlink, No = copy.", QMessageBox::Yes | QMessageBox::No);
    bool symlink = (ret == QMessageBox::Yes);

    // Capture the outgoing profile name before the importer makes the new one
    // active (and frees the old object) - promptSwitchToNewProfile falls back to
    // it on "No".
    const QString priorActive =
        m_profileMgr->activeProfile() ? m_profileMgr->activeProfile()->name() : QString();

    statusBar()->showMessage("Importing MO2 profile...");
    qApp->processEvents();
    auto r = solero::Mo2Importer::importProfile(profileDir, modsDir,
        solero::AppConfig::instance().stagingDir(), *m_profileMgr, name.trimmed(), symlink);
    if (!r.success) { QMessageBox::critical(this, "Import Failed", r.errorMessage); return; }

    selectImportedProfile(r.profileName, priorActive);
    statusBar()->showMessage(QString("Imported '%1' - %2 mods staged.").arg(r.profileName).arg(r.modsStaged));
    // MO2 lists often keep SKSE's loader in the game root (outside the mods), so
    // the import won't include it - offer to install SKSE if it's missing.
    QTimer::singleShot(0, this, &MainWindow::ensureSkse);
}

void MainWindow::onExportProfile() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile to export."); return; }

    const QString suggested =
        QDir::homePath() + "/" + profile->name() + ".solero-profile.json";
    QString path = QFileDialog::getSaveFileName(
        this, "Export Profile", suggested,
        "Solero profile (*.solero-profile.json);;All files (*)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".solero-profile.json", Qt::CaseInsensitive))
        path += ".solero-profile.json";

    const QString fomodDir = solero::AppConfig::dataRoot() + "/fomod-choices";
    if (solero::ProfileManifest::exportToFile(*profile, path, fomodDir))
        statusBar()->showMessage("Exported profile to " + path);
    else
        QMessageBox::critical(this, "Export Failed", "Could not write " + path);
}

void MainWindow::onImportProfile() {
    QString path = QFileDialog::getOpenFileName(
        this, "Import Profile", QDir::homePath(),
        "Solero profile (*.solero-profile.json);;All files (*)");
    if (path.isEmpty()) return;

    // Build the mod pool: the union of every profile's mod entries, deduped by id.
    // (A mod is "installed" once it exists in any profile's modlist; its staging
    // folder is keyed by that id, so reusing the id lets deploy find the files.)
    solero::ModList pool;
    QSet<QString> seen;
    for (const QString& pn : m_profileMgr->profileNames()) {
        solero::Profile pr(pn, profilesRoot());
        pr.load();
        for (auto it = pr.modList().begin(); it != pr.modList().end(); ++it) {
            if (it->type != solero::EntryType::Mod) continue;
            if (seen.contains(it->id)) continue;
            seen.insert(it->id);
            pool.append(*it);
        }
    }

    // Capture the outgoing profile name before the importer makes the new one
    // active (and frees the old object).
    const QString priorActive =
        m_profileMgr->activeProfile() ? m_profileMgr->activeProfile()->name() : QString();

    const QString fomodDir = solero::AppConfig::dataRoot() + "/fomod-choices";
    auto r = solero::ProfileManifest::importFile(path, *m_profileMgr, pool, fomodDir);
    if (!r.success) { QMessageBox::critical(this, "Import Failed", r.errorMessage); return; }

    selectImportedProfile(r.profileName, priorActive);

    const QString summary =
        QString("Imported profile '%1': %2 mods matched, %3 separators, %4 missing (listed).")
            .arg(r.profileName).arg(r.modsMatched).arg(r.separators).arg(r.missing.size());
    statusBar()->showMessage(summary);

    if (r.missing.isEmpty()) {
        QMessageBox::information(this, "Profile Imported", summary);
        return;
    }
    const QString bullet = QString(QChar(0x2022)) + " ";
    QString body = summary +
        "\n\nThese mods aren't installed on this machine and were skipped:\n";
    for (const auto& m : r.missing) {
        body += "\n  " + bullet + m.name;
        if (!m.version.isEmpty())    body += " (" + m.version + ")";
        if (!m.nexusModId.isEmpty()) body += "  - Nexus mod " + m.nexusModId;
    }
    body += "\n\nInstall them, then re-import the manifest (or add them to this profile).";
    QMessageBox::information(this, "Profile Imported - Missing Mods", body);
}

void MainWindow::selectImportedProfile(const QString& name, const QString& priorActiveName) {
    // The importer loaded the new profile into the ProfileManager (m_active), which
    // FREED the previously-active Profile - but the view models still hold that raw
    // pointer. Detach them now so a paint during the prompt or switchProfile's
    // deploy/undeploy pump can't deref the dangling old profile. This is safe on
    // both answers: Yes re-attaches the new profile's views via switchProfile, and
    // No reloads priorActiveName (which also re-attaches its views).
    detachProfileFromViews();
    refreshProfileCombo();
    { QSignalBlocker b(m_profileCombo); m_profileCombo->setCurrentText(name); }
    promptSwitchToNewProfile(name, priorActiveName);
}

void MainWindow::onInstallWabbajack() {
    // Capture the outgoing profile name before the dialog's import makes the new
    // one active (and frees the old object) - used as the "No" fallback.
    const QString priorActive =
        m_profileMgr->activeProfile() ? m_profileMgr->activeProfile()->name() : QString();
    solero::WabbajackDialog dlg(m_profileMgr, this);
    connect(&dlg, &solero::WabbajackDialog::profileImported, this,
            [this, priorActive](const QString& name, const QList<solero::ImportedTool>& tools) {
        selectImportedProfile(name, priorActive);
        statusBar()->showMessage(QString("Imported Wabbajack modlist '%1'.").arg(name));
        // The profile is now active - auto-configure the modlist's tools against
        // it (output mods belong to this profile; tools go to the global store).
        setUpImportedTools(tools);
        // After the profile switch settles, detect a missing SKSE and offer it.
        QTimer::singleShot(0, this, &MainWindow::ensureSkse);
    });
    dlg.exec();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Zoom (Ctrl +/-/0) is now owned by the View ▸ Zoom menu actions' shortcuts;
    // other global shortcuts are bound as QActions / QShortcuts in setup*().
    QMainWindow::keyPressEvent(event);
}

void MainWindow::onZoomIn()    { static_cast<Application*>(qApp)->setZoomFactor(static_cast<Application*>(qApp)->zoomFactor() + 0.1); }
void MainWindow::onZoomOut()   { static_cast<Application*>(qApp)->setZoomFactor(static_cast<Application*>(qApp)->zoomFactor() - 0.1); }
void MainWindow::onZoomReset() { static_cast<Application*>(qApp)->setZoomFactor(1.0); }

void MainWindow::onRunTool(const solero::Executable& exe) {
    if (m_deploying) {
        statusBar()->showMessage("A deploy is in progress - please wait for it to finish.");
        return;
    }
    if (m_toolRunning) {
        statusBar()->showMessage("A tool is running - please wait for it to finish.");
        return;
    }
    if (!solero::AppConfig::instance().isConfigured()) {
        QMessageBox::warning(this, "Tool", "Configure the game first (Game Settings\xe2\x80\xa6).");
        return;
    }
    // Tools operate on the live Data/ folder, so the modlist must be deployed & current.
    if (!ensureDeployed("running this tool")) return;

    // Lock the UI (MO2-style) while the tool runs. m_toolRunning stays true for
    // the whole run - even if the user dismisses the overlay via "Unlock Solero" -
    // so the re-entrancy guards remain active until the run actually finishes.
    m_toolRunning = true;
    showRunLock(exe.name);

    // A tool's output mod may be unset - either never configured, or MIGRATED
    // into this profile (migration leaves outputModId empty) - or it may point at
    // a mod that's no longer in the active profile. For a producesOutput tool,
    // auto-create-or-reuse the per-profile output mod on first run (frictionless:
    // no error, no dialog) and persist it on the STORED Executable so subsequent
    // runs reuse it. exe is a copy from the menu; we must update the entry in
    // activeTools(), not this local copy.
    QString resolvedOutputModId = exe.outputModId;
    if (const auto* preset = solero::ToolCatalog::byId(exe.id); preset && preset->producesOutput) {
        auto* p = m_profileMgr->activeProfile();
        const bool unset = exe.outputModId.isEmpty();
        const bool stale = !unset && p &&
            !p->modList().findById(exe.outputModId);
        if (unset || stale) {
            const QString name = preset->outputModName.isEmpty()
                ? exe.name + " Output" : preset->outputModName;
            const QString id = ensureOutputMod(name);
            if (!id.isEmpty()) {
                resolvedOutputModId = id;
                // Persist on the stored Executable (by id, else by name).
                for (auto& t : activeTools()) {
                    const bool match = exe.id.isEmpty()
                        ? t.name == exe.name : t.id == exe.id;
                    if (match) { t.outputModId = id; break; }
                }
                saveActiveTools();
            }
        }
    }

    QString outFolder;
    if (auto* p = m_profileMgr->activeProfile(); p && !resolvedOutputModId.isEmpty())
        outFolder = p->stagingFolderFor(resolvedOutputModId);

    // Tools read the deployed (merged) game Data. If the tool's own output mod is
    // deployed, the tool ingests its own prior output (double-patching, re-scanning
    // already-processed plugins/assets). MO2 users disable a tool's output mod
    // before re-running it; Solero is VFS-less, so "disable" only takes effect
    // after a redeploy removes the output mod's files from the game Data. For ANY
    // tool with an output mod, toggle it off in memory (never persisted - a mid-run
    // crash leaves the on-disk profile enabled), redeploy, run, then re-enable +
    // redeploy to bring the fresh output back. Skip entirely on first run (no
    // staged output to interfere with). restoreOutput() runs before every
    // early-return and once on the normal path; it's idempotent via the guard.
    auto* tp = m_profileMgr->activeProfile();
    const bool wantToggle = tp && !resolvedOutputModId.isEmpty()
                            && outputModHasStagedFiles(tp, resolvedOutputModId);
    bool toggledOutput = false;
    auto restoreOutput = [&]() {
        if (toggledOutput && tp) {
            tp->modList().setEnabled(resolvedOutputModId, true);   // in memory only, no save
            redeployForTool("Restoring output mod", "Re-deploying with the output mod\xe2\x80\xa6");
            toggledOutput = false;
        }
    };
    if (wantToggle) {
        tp->modList().setEnabled(resolvedOutputModId, false);      // in memory only, no profile.save()
        if (!redeployForTool("Preparing " + exe.name, "Temporarily removing the output mod\xe2\x80\xa6")) {
            tp->modList().setEnabled(resolvedOutputModId, true);   // restore in memory
            hideRunLock(); m_toolRunning = false;
            QMessageBox::warning(this, exe.name, "Could not temporarily remove the output mod for this run. Make sure the output mod is configured in the tool's settings, then try again.");
            return;
        }
        toggledOutput = true;
    }

    // Radium expects an MO2 layout. Solero hardlink-deploys into Data/, so feed
    // it a generated fake-mo2 + a settings.json pointing at the live Data/ and the
    // managed output mod. (ensureDeployed above guarantees Data/ + load order are
    // current.)
    if (exe.id == "radium") {
        auto* p = m_profileMgr->activeProfile();
        // Radium writes into its output mod's staging Data/; without an output
        // mod wired we'd misdirect its result to the staging root. Fail clearly.
        if (outFolder.isEmpty()) {
            restoreOutput();
            hideRunLock();
            m_toolRunning = false;
            QMessageBox::warning(this, "Radium Textures",
                "No output mod is configured for Radium. Re-add it via Tools \xe2\x96\xb8 Add tool.");
            return;
        }
        const QString gameDir = solero::AppConfig::instance().gameDir();
        const QString outDataDir =
            solero::AppConfig::instance().stagingDir() + "/" + outFolder + "/Data";
        QString err;
        if (!p || !solero::RadiumPrep::prepare(*p, gameDir,
                                               QFileInfo(exe.binaryPath).path(),
                                               outDataDir,
                                               solero::RadiumPrep::defaultSettingsPath(),
                                               &err)) {
            restoreOutput();
            hideRunLock();
            m_toolRunning = false;
            QMessageBox::warning(this, "Radium Textures",
                err.isEmpty() ? "Could not write Radium's configuration files. Make sure the staging folder is writable." : err);
            return;
        }
    }

    // PGPatcher also expects an MO2 instance. It reads its own cfg/settings.json
    // (MO2 mode, pointed at a fake-mo2 dir); regenerate that instance so the load
    // order is current, then merge-write settings.json so the launcher opens
    // pre-populated (Game, MO2 + a valid instance, Output) - which also fixes the
    // "missing ModOrganizer.ini" error on selecting MO2. We merge (not overwrite)
    // to preserve the user's shader/patcher toggles. The --ignore-mo2vfscheck flag
    // (added below) stops PGPatcher aborting on "VFS not detected" outside a real
    // MO2 usvfs.
    if (exe.id == "pgpatcher") {
        auto* p = m_profileMgr->activeProfile();
        const QString installDir = QFileInfo(exe.binaryPath).path();
        QString fakeMo2 = installDir + "/fake-mo2"; // fallback if unconfigured
        QFile sf(installDir + "/cfg/settings.json");
        if (sf.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(sf.readAll()).object();
            QString mm = root.value("params").toObject()
                             .value("modmanager").toObject()
                             .value("mo2instancedir").toString();
            if (!mm.isEmpty()) {
                mm.replace('\\', '/');                 // Windows -> native separators
                mm.remove(QRegularExpression("^[A-Za-z]:")); // strip drive (Z: -> /)
                fakeMo2 = mm;
            }
        }
        QString err;
        // PGPatcher runs in None mode (reads the deployed game Data), so it does
        // not consume the MO2 instance's mods/+modlist - keep populateMods=false to
        // skip symlinking the whole profile each launch. (The populate path stays
        // available via writeFakeMo2 if MO2 mode is ever revisited; see
        // PgpatcherConfig modmanager comment for why MO2 mode is disabled.)
        if (!p || !solero::RadiumPrep::writeFakeMo2(
                *p, solero::AppConfig::instance().gameDir(), fakeMo2, &err,
                /*populateMods=*/false)) {
            restoreOutput();
            hideRunLock(); m_toolRunning = false;
            QMessageBox::warning(this, "PGPatcher",
                err.isEmpty() ? "Could not prepare PGPatcher's working files. Make sure the tool's install folder is writable." : err);
            return;
        }

        // Merge-write cfg/settings.json so the launcher opens pre-populated. The
        // output dir is the output mod's <folder>/Data subdir - the same place
        // Solero's other tool outputs live and where the deployer maps to the
        // game's Data/. (PGPatcher only rejects a path inside the GAME's Data
        // folder; the mod's own Data subdir is fine.) Pointing it at the mod root
        // tripped PGPatcher's "output dir has non-PGPatcher files" check on the
        // empty Data/ scaffold + Solero metadata sitting in the root.
        const QString cfgDir = installDir + "/cfg";
        QDir().mkpath(cfgDir);
        QJsonObject existing;
        if (QFile rf(cfgDir + "/settings.json"); rf.open(QIODevice::ReadOnly))
            existing = QJsonDocument::fromJson(rf.readAll()).object();
        const QString outputDir = resolvedOutputModId.isEmpty()
            ? QString()
            : solero::AppConfig::instance().stagingDir() + "/" +
                  p->stagingFolderFor(resolvedOutputModId) + "/Data";
        const QJsonObject merged = solero::PgpatcherConfig::buildSettings(
            existing, solero::AppConfig::instance().gameDir(), fakeMo2, outputDir);
        if (QFile wf(cfgDir + "/settings.json"); wf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            wf.write(QJsonDocument(merged).toJson(QJsonDocument::Indented));
    }

    auto* owP = m_profileMgr->activeProfile();
    solero::Executable runExe = exe;
    if (runExe.id == "pgpatcher" && !runExe.arguments.contains("--ignore-mo2vfscheck"))
        runExe.arguments = (runExe.arguments.trimmed() + " --ignore-mo2vfscheck").trimmed();
    auto res = solero::ToolRunner::run(runExe, solero::AppConfig::instance().gameDir(),
                                       solero::AppConfig::instance().stagingDir(), outFolder,
                                       owP ? solero::AppConfig::overwriteDir(owP->name()) : QString());
    hideRunLock();
    m_toolRunning = false;

    // Re-enable the output mod and redeploy so the tool's fresh output returns to
    // the deployed game Data. Idempotent (no-op if we never toggled).
    restoreOutput();

    if (!res.launched) {
        QMessageBox::warning(this, "Tool",
            looksHumanReadable(res.error) ? res.error
                : (exe.name + " could not be launched. Make sure the binary path is correct in the tool's settings (right-click → Edit)."));
    } else if (!res.output.isEmpty()) {
        // Surface tool output (helps diagnose e.g. wine 'Not implemented').
        statusBar()->showMessage(exe.name + " finished.");
    }
    // A tool may have captured output into an output mod, changing staged files -
    // invalidate all cached scans so empties/highlights re-read fresh.
    m_modListView->invalidateModCache();
    m_rightPane->invalidateModPluginCache();
    if (auto* p = m_profileMgr->activeProfile()) { m_rightPane->refreshPlugins(p); m_modListView->setProfile(p); }
    updatePluginNotice();
}

void MainWindow::showRunLock(const QString& toolName) {
    if (!m_runOverlay) {
        m_runOverlay = new QWidget(this);
        m_runOverlay->setStyleSheet("background: rgba(0,0,0,140);");
        auto* lay = new QVBoxLayout(m_runOverlay);
        m_runLockLabel = new QLabel(m_runOverlay);
        m_runLockLabel->setAlignment(Qt::AlignCenter);
        m_runLockLabel->setStyleSheet("color:white; font-size:16px; background: transparent;");
        m_unlockBtn = new QPushButton("Unlock Solero", m_runOverlay);
        m_unlockBtn->setToolTip("The tool keeps running in the background.");
        connect(m_unlockBtn, &QPushButton::clicked, this, [this] { hideRunLock(); });
        lay->addStretch();
        lay->addWidget(m_runLockLabel, 0, Qt::AlignHCenter);
        lay->addSpacing(12);
        lay->addWidget(m_unlockBtn, 0, Qt::AlignHCenter);
        lay->addStretch();
    }
    m_runLockLabel->setText("\xf0\x9f\x94\x92  " + toolName + " is running.\nSolero is locked until it closes.");
    m_runOverlay->setGeometry(rect());
    m_runOverlay->raise();
    m_runOverlay->show();
    qApp->processEvents();
}

void MainWindow::hideRunLock() { if (m_runOverlay) m_runOverlay->hide(); }

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_runOverlay && m_runOverlay->isVisible()) m_runOverlay->setGeometry(rect());
}

QString MainWindow::ensureOutputMod(const QString& name) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return {};

    // Reuse an existing mod with this name in the active profile's mod list.
    for (const auto& m : profile->modList())
        if (m.type == solero::EntryType::Mod && m.name == name)
            return m.id;

    // Create a fresh output mod (mirrors installFromArchive's add+save+refresh).
    solero::ModEntry mod;
    mod.type = solero::EntryType::Mod;
    mod.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mod.name = name;
    mod.enabled = true;
    mod.isOutputMod = true;
    // Profile-qualify the on-disk folder (keep the DISPLAY name plain) so two
    // profiles both producing e.g. "PGPatcher Output" don't collide on the same
    // shared mods/ folder - matching the existing "CSVO - Dyndolod Output" convention.
    mod.stagingFolder = solero::uniqueStagingFolder(
        solero::sanitizeStagingFolder(profile->name() + " - " + name),
        takenStagingFolders(profile));

    QDir().mkpath(solero::stagingPathFor(
        solero::AppConfig::instance().stagingDir(), mod) + "/Data");

    profile->modList().append(mod);
    checkSave(profile->save());
    m_modListView->setProfile(profile);

    return mod.id;
}

QString MainWindow::chooseOutputMod(const QString& defaultName, const QString& toolName) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return {};

    // Exact-name match -> reuse silently (e.g. a mod we created before, or an
    // imported list's output mod). Tag it as an output mod so it gets the output
    // styling/capture semantics (the fuzzy path below does the same).
    for (const auto& m : profile->modList())
        if (m.type == solero::EntryType::Mod && m.name == defaultName) {
            if (!m.isOutputMod) {
                solero::ModEntry up = m; up.isOutputMod = true;
                profile->modList().update(up.id, up); profile->save();
            }
            return m.id;
        }

    // Fuzzy: an imported/Wabbajack list usually ships the tool's output mod under
    // a slightly different name. Suggest mods that share the tool's keyword and
    // look like an output target (excluding the tool's *Resources* mod).
    QString key = defaultName; key.remove(" Output", Qt::CaseInsensitive); key = key.trimmed();
    QList<QPair<QString,QString>> matches; // (name, id)
    for (const auto& m : profile->modList()) {
        if (m.type != solero::EntryType::Mod) continue;
        if (key.isEmpty() || !m.name.contains(key, Qt::CaseInsensitive)) continue;
        if (m.name.contains("Resource", Qt::CaseInsensitive)) continue; // resources != output
        const bool looksOutput = m.isOutputMod
            || m.name.contains("Output", Qt::CaseInsensitive)
            || m.name.compare(key, Qt::CaseInsensitive) == 0;
        if (looksOutput) matches << qMakePair(m.name, m.id);
    }
    if (matches.isEmpty())
        return ensureOutputMod(defaultName); // nothing to suggest -> create ours

    QStringList items;
    for (const auto& mp : matches) items << mp.first;
    const QString createLabel = QString("\xe2\x9e\x95 Create new \"%1\"").arg(defaultName);
    items << createLabel;
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, "Output mod for " + toolName,
        QString("Solero found an existing mod that looks like %1's output.\n"
                "Use it as the output target, or create a new one?").arg(toolName),
        items, 0, /*editable=*/false, &ok);
    if (!ok || chosen == createLabel)
        return ensureOutputMod(defaultName);
    for (const auto& mp : matches)
        if (mp.first == chosen) {
            // Tag the chosen mod as an output mod (colour + capture semantics).
            if (auto* e = profile->modList().findById(mp.second); e && !e->isOutputMod) {
                solero::ModEntry up = *e; up.isOutputMod = true;
                profile->modList().update(up.id, up); profile->save();
            }
            return mp.second;
        }
    return ensureOutputMod(defaultName);
}

void MainWindow::onAddTool2() {
    // Build the PER-profile installed set so the wizard's "(installed)" badge
    // reflects this profile's executables, not the shared global template. Key by
    // each tool's id and its lower-cased name (a preset matches on either).
    QSet<QString> installedKeys;
    for (const auto& t : activeTools()) {
        if (!t.id.isEmpty())   installedKeys.insert(t.id);
        if (!t.name.isEmpty()) installedKeys.insert(t.name.toLower());
    }
    solero::ToolSetupWizard dlg(this, m_toolStore, installedKeys);
    dlg.setModChoices(modChoices());
    connect(&dlg, &solero::ToolSetupWizard::installModRequested,
            this, &MainWindow::installFromArchive);
    dlg.exec();
    // Add only the tool(s) the user actually set up in this wizard session to the
    // active profile - never the rest of the global store (the wizard still writes
    // each set-up tool into m_toolStore as a harmless template, but seeding a
    // profile from the whole store would re-inject unrelated tools from earlier
    // sessions and falsely mark them "(installed)" here).
    auto& profileTools = activeTools();
    bool changedAny = false;
    for (const auto& e : dlg.setUpTools()) {
        const QString key = e.id.isEmpty() ? e.name.toLower() : e.id;
        bool upserted = false;
        for (auto& t : profileTools) {
            const QString tkey = t.id.isEmpty() ? t.name.toLower() : t.id;
            if (tkey == key) { t = e; upserted = true; break; }
        }
        if (!upserted) profileTools.append(e);
        changedAny = true;
    }
    // Wire output mods for any set-up tool that produces output. This is the
    // frictionless WIZARD path: auto-create-or-reuse the per-profile output mod
    // with a friendly name + staging folder (NO QInputDialog). The interactive
    // picker (chooseOutputMod) is reserved for the manual "Edit Tool" path.
    for (auto& exe : profileTools) {
        const auto* preset = solero::ToolCatalog::byId(exe.id);
        if (!preset) continue;
        if (preset->producesOutput && exe.outputModId.isEmpty()) {
            exe.outputModId = ensureOutputMod(preset->outputModName);
            exe.isCapturingOutput = !preset->writesOutputDirectly;
            changedAny = true;
        }
        // secondary actions: match by index to the preset's extraActions
        for (int i = 0; i < exe.extraActions.size() && i < preset->extraActions.size(); ++i) {
            if (exe.extraActions[i].outputModId.isEmpty()
                && !preset->extraActions[i].outputModName.isEmpty()) {
                exe.extraActions[i].outputModId =
                    ensureOutputMod(preset->extraActions[i].outputModName);
                changedAny = true;
            }
        }
    }
    if (changedAny) saveActiveTools();
    rebuildToolsMenu();
}

void MainWindow::setUpImportedTools(const QList<solero::ImportedTool>& tools) {
    if (tools.isEmpty()) return;
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;

    // Skyrim Proton prefix (compatdata/489830), derived from localAppData up to
    // /pfx - same derivation the Set-Up-Tool wizard uses.
    QString lad = solero::AppConfig::instance().localAppDataDir();
    const int pfxAt = lad.indexOf("/pfx");
    const QString winePrefix = pfxAt > 0 ? lad.left(pfxAt) : QString();

    // Existing tool ids/names in the global store (idempotency: don't duplicate
    // on a re-import of the same list).
    QSet<QString> haveIds, haveNames;
    for (const auto& t : m_toolStore->tools()) {
        haveIds.insert(t.id);
        haveNames.insert(t.name.toLower());
    }

    QStringList configured;  // "xEdit -> SSEEdit (xEdit)" lines for the summary
    QStringList unresolved;  // "<name> - <reason>" lines for the summary
    bool storeChanged = false;

    // Tools are per-profile now: register each discovered tool into the global
    // template (m_toolStore, for future-profile seeding) and into the active
    // profile's executables (what the user actually sees/runs). Upsert by id (or
    // name when id-less) so a re-import updates in place rather than duplicating.
    auto& profileTools = profile->executables();
    auto upsertIntoProfile = [&profileTools](const solero::Executable& e) {
        for (auto& t : profileTools) {
            const bool sameId   = !e.id.isEmpty() && t.id == e.id;
            const bool sameName = e.id.isEmpty() && t.name.compare(e.name, Qt::CaseInsensitive) == 0;
            if (sameId || sameName) { t = e; return; }
        }
        profileTools.append(e);
    };

    for (const solero::ImportedTool& it : tools) {
        const QString presetId = solero::presetIdForToolName(it.name, it.binary);
        const bool present = QFileInfo::exists(it.binary);

        // Idempotent skip: already configured by id (mapped) or by name (custom).
        if ((!presetId.isEmpty() && haveIds.contains(presetId))
            || haveNames.contains(it.name.toLower())) {
            continue;
        }

        if (!presetId.isEmpty()) {
            const solero::ToolPreset* p = solero::ToolCatalog::byId(presetId);
            if (!p) { continue; }
            if (!present) {
                // DEFAULT: don't auto-download a mapped tool the list didn't ship.
                unresolved << QString("%1 - not shipped by the list; install it via "
                                      "Tools ▸ Add tool").arg(p->name);
                continue;
            }
            solero::Executable e = solero::ToolSetup::buildExecutable(*p, it.binary, winePrefix);
            // Carry the list's own configured arguments when it set any (they often
            // pin -D:"…/Data"); otherwise keep the preset's defaults.
            if (!it.args.trimmed().isEmpty()) e.arguments = it.args;

            // Wire the primary output mod (match-or-create against this profile).
            if (p->producesOutput) {
                e.outputModId = chooseOutputMod(p->outputModName, p->name);
                e.isCapturingOutput = !p->writesOutputDirectly;
            }
            // Wire secondary-action output mods (match preset extraActions by index).
            for (int i = 0; i < e.extraActions.size() && i < p->extraActions.size(); ++i) {
                if (!p->extraActions[i].outputModName.isEmpty()) {
                    e.extraActions[i].outputModId = chooseOutputMod(
                        p->extraActions[i].outputModName,
                        p->name + " (" + p->extraActions[i].label + ")");
                }
            }
            m_toolStore->update(e);
            upsertIntoProfile(e);
            haveIds.insert(presetId);
            haveNames.insert(e.name.toLower());
            storeChanged = true;
            configured << QString("%1 → %2").arg(it.name, p->name);
        } else {
            // Unmapped tool.
            if (!present) {
                unresolved << QString("%1 - binary not found").arg(it.name);
                continue;
            }
            solero::Executable e;
            e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            e.name = it.name;
            e.binaryPath = it.binary;
            e.arguments = it.args;
            // Imported MO2 binaries are Windows exes run under Proton.
            e.runtime = it.binary.endsWith(".exe", Qt::CaseInsensitive)
                            ? solero::RuntimeType::Proton : solero::RuntimeType::Native;
            e.protonVersion = QFileInfo(
                solero::AppConfig::instance().detectProtonDir()).fileName();
            e.winePrefix = winePrefix;
            e.runThroughDeployer = false;
            // Wire output only if the list already ships a matching "…Output" mod
            // (no create for custom tools - leave Overwrite otherwise).
            const QString outId = solero::findOutputModId(profile->modList().entries(), it.name);
            if (!outId.isEmpty()) { e.outputModId = outId; e.isCapturingOutput = true; }
            m_toolStore->update(e);
            upsertIntoProfile(e);
            haveNames.insert(e.name.toLower());
            storeChanged = true;
            configured << QString("%1 (custom)").arg(it.name);
        }
    }

    if (storeChanged) { m_toolStore->save(); checkSave(profile->save()); rebuildToolsMenu(); }

    // Summary (non-blocking; skipped when nothing was discovered/actionable).
    if (configured.isEmpty() && unresolved.isEmpty()) return;
    QString body;
    if (!configured.isEmpty())
        body += QString("Set up %1 tool%2:\n  • %3\n")
                    .arg(configured.size())
                    .arg(configured.size() == 1 ? "" : "s")
                    .arg(configured.join("\n  • "));
    if (!unresolved.isEmpty()) {
        if (!body.isEmpty()) body += "\n";
        body += QString("Couldn't set up %1:\n  • %2")
                    .arg(unresolved.size())
                    .arg(unresolved.join("\n  • "));
    }
    auto* box = new QMessageBox(QMessageBox::Information, "Modlist tools", body,
                                QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->show();
}

QList<solero::Executable>& MainWindow::activeTools() {
    static QList<solero::Executable> empty;
    if (auto* p = m_profileMgr->activeProfile()) return p->executables();
    return empty;
}

void MainWindow::saveActiveTools() {
    if (auto* p = m_profileMgr->activeProfile()) checkSave(p->save());
}

void MainWindow::migrateToolsToActiveProfileOnce() {
    // one-time app-level migration: fold the legacy global tool template into the
    // active profile only. New/other profiles stay empty - there is no
    // per-activation seeding. Gated by the toolsMigratedToPerProfile flag so it
    // runs exactly once across the app's lifetime.
    if (solero::AppConfig::instance().toolsMigratedToPerProfile()) return;
    auto* p = m_profileMgr->activeProfile();
    if (!p) return;
    // Re-resolve each template tool's output mod against this profile: derive the
    // expected output-mod name from the matching ToolCatalog preset (fall back to
    // "<tool> Output"), then match a same-named output mod in this profile's
    // modlist (or empty to defer creation).
    auto resolver = [p](const solero::Executable& t) -> QString {
        QString outName;
        for (const auto& preset : solero::ToolCatalog::presets()) {
            if (preset.name.compare(t.name, Qt::CaseInsensitive) == 0) {
                outName = preset.outputModName;
                break;
            }
        }
        if (outName.isEmpty()) outName = t.name + " Output";
        return solero::Profile::matchOutputModId(p->modList(), outName);
    };
    // seedExecutablesFrom is a no-op if the profile already has executables
    // in memory (e.g. CSVO with its real PGPatcher+Radium), so an existing setup
    // is never clobbered. Either way we mark the migration done.
    p->seedExecutablesFrom(m_toolStore->tools(), resolver);
    solero::AppConfig::instance().setToolsMigratedToPerProfile(true);
    solero::AppConfig::instance().save();
}

void MainWindow::rebuildToolsMenu() {
    if (!m_toolsMenu) return;
    m_toolsMenu->clear();
    // Display a name-sorted COPY (case-insensitive); the underlying store order is
    // left untouched because post-deploy auto-run order depends on it.
    auto tools = activeTools();
    std::sort(tools.begin(), tools.end(), [](const solero::Executable& a, const solero::Executable& b){
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    for (const auto& exe : tools) {
        if (exe.extraActions.isEmpty()) {
            // Click the tool name to run it.
            solero::Executable primary = exe;
            m_toolsMenu->addAction(QIcon(exe.iconPath), exe.name, this, [this, primary]{ onRunTool(primary); });
        } else {
            // Tool with secondary actions: a submenu of run targets.
            QMenu* sub = m_toolsMenu->addMenu(QIcon(exe.iconPath), exe.name);
            solero::Executable primary = exe;
            sub->addAction("\xe2\x96\xb6 Run " + exe.name, this, [this, primary]{ onRunTool(primary); });
            for (const auto& a : exe.extraActions) {
                solero::Executable ax = primary;
                ax.binaryPath = a.binaryPath; ax.arguments = a.arguments;
                ax.outputModId = a.outputModId; ax.isCapturingOutput = !a.outputModId.isEmpty();
                ax.extraActions.clear();
                sub->addAction("\xe2\x96\xb6 " + a.label, this, [this, ax]{ onRunTool(ax); });
            }
        }
    }
    if (!tools.isEmpty()) m_toolsMenu->addSeparator();
    // Built-ins: always available regardless of configured tools.
    m_toolsMenu->addAction(QIcon::fromTheme("system-search", QIcon::fromTheme("edit-find")),
                           QStringLiteral("Patch Wizard") + QChar(0x2026), this, &MainWindow::onPatchWizard);
    // BethINI (INI editor) folded into Tools per the declutter spec.
    m_toolsMenu->addAction(QIcon::fromTheme("preferences-system"),
                           QStringLiteral("BethINI") + QChar(0x2026), this, &MainWindow::onOpenBethini);
    m_toolsMenu->addSeparator();
    // Add and Manage are one entry: the manager dialog hosts add / edit / remove.
    m_toolsMenu->addAction(QIcon::fromTheme("configure", QIcon::fromTheme("applications-utilities")),
                           QStringLiteral("Add or Manage Tools") + QChar(0x2026), this, &MainWindow::onManageTools);
}

QList<QPair<QString,QString>> MainWindow::modChoices() const {
    QList<QPair<QString,QString>> out;
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return out;
    for (const auto& m : profile->modList())
        if (m.type == solero::EntryType::Mod)
            out.append({m.id, m.name});
    return out;
}

void MainWindow::onManageTools() {
    solero::ToolsManagerDialog dlg(&activeTools(), this);
    connect(&dlg, &solero::ToolsManagerDialog::addToolRequested, this, [this, &dlg]{ onAddTool2(); dlg.refresh(); });
    connect(&dlg, &solero::ToolsManagerDialog::editToolRequested, this, [this, &dlg](const QString& id){ onEditTool(id); dlg.refresh(); });
    connect(&dlg, &solero::ToolsManagerDialog::removeToolRequested, this, [this, &dlg](const QString& id){ onRemoveTool(id); dlg.refresh(); });
    dlg.exec();
}

void MainWindow::onPatchWizard() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile."); return; }
    if (!solero::AppConfig::instance().isConfigured()) {
        QMessageBox::warning(this, "Patch Wizard",
                             "Configure the game first (Game Settings\xe2\x80\xa6).");
        return;
    }
    solero::PatchWizardDialog dlg(profile, this);
    connect(&dlg, &solero::PatchWizardDialog::patchesInstalled, this,
            [this](const QStringList& modIds) {
        for (const QString& id : modIds) {
            m_modListView->invalidateModCache(id);
            m_rightPane->invalidateModPluginCache(id);
            solero::DependencyChecker::invalidate(id);
        }
        if (auto* p = m_profileMgr->activeProfile()) m_rightPane->refreshPlugins(p);
        if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
        updatePluginNotice();
        statusBar()->showMessage("Patches installed - re-deploy to apply.");
    });
    dlg.exec();
}

void MainWindow::onOpenBethini() {
    if (!m_bethiniWindow) return;
    if (auto* p = m_profileMgr->activeProfile()) m_bethiniWindow->setProfile(p);
    m_bethiniWindow->setWindowFlag(Qt::Window, true);
    m_bethiniWindow->setWindowModality(Qt::ApplicationModal);
    m_bethiniWindow->setWindowTitle("BethINI - Skyrim INI Editor");
    m_bethiniWindow->resize(1150, 820);
    m_bethiniWindow->show();
    m_bethiniWindow->raise();
    m_bethiniWindow->activateWindow();
}

void MainWindow::onOpenLootRules() {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile."); return; }

    QDialog dlg(this);
    dlg.setWindowTitle("LOOT Rules");
    dlg.resize(700, 500);
    auto* layout = new QVBoxLayout(&dlg);
    auto* editor = new solero::LootRulesEditor(&dlg);
    editor->setProfile(profile);
    layout->addWidget(editor);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(buttons);
    dlg.exec();
}

void MainWindow::onPlay() {
    if (m_deploying) {
        statusBar()->showMessage("A deploy is in progress - wait for it to finish.");
        return;
    }
    if (m_toolRunning) {
        statusBar()->showMessage("Something is already running - wait for it to finish.");
        return;
    }
    if (!solero::AppConfig::instance().isConfigured()) {
        QMessageBox::warning(this, "Play", "Configure the game first (Game Settings\xe2\x80\xa6).");
        return;
    }
    // Grey the Play button from click until the game closes; the scope guard
    // restores it on every exit path (early returns and the normal post-run path,
    // which returns only after the game has closed).
    if (m_playAction) m_playAction->setEnabled(false);
    auto restorePlay = qScopeGuard([this]{ if (m_playAction) m_playAction->setEnabled(true); });

    // Mods must be deployed to play with them (same as running a tool).
    if (!ensureDeployed("playing")) return;

    // Reconcile the active profile's INIs to the live (prefix Documents) copies
    // on every launch. Deploy syncs them, but deploy is skipped above when the
    // modlist is already deployed-and-clean - and the game reads the live copy,
    // not the profile. Without this, a stale live SkyrimPrefs.ini (e.g. one an
    // in-game graphics-settings save overwrote) silently overrides the BethINI
    // preset the user applied. Idempotent if deploy just did the same copy.
    if (auto* prof = m_profileMgr->activeProfile()) {
        QString docs = solero::AppConfig::instance().documentsDir();
        QString iniDir = docs.isEmpty() ? solero::AppConfig::instance().gameDir() : docs;
        if (!iniDir.isEmpty()) {
            QDir().mkpath(iniDir);
            const QStringList iniSrc = { prof->skyrimIniPath(), prof->skyrimPrefsPath(), prof->skyrimCustomPath() };
            const QStringList iniDst = { iniDir + "/Skyrim.ini", iniDir + "/SkyrimPrefs.ini", iniDir + "/SkyrimCustom.ini" };
            for (int i = 0; i < iniSrc.size(); ++i)
                if (QFile::exists(iniSrc[i])) solero::copyOverwrite(iniSrc[i], iniDst[i]);
        }
    }

    // Re-assert the managed shader cache into the live game dir on every launch,
    // for the same reason as the INI re-sync above: Community Shaders OWNS
    // Data/ShaderCache at runtime and DELETES the whole tree whenever it invalidates
    // the cache, silently removing the hardlinks we deployed. ensureDeployed() skips
    // redeploy when the modlist is deployed-and-clean, so the staged Info.ini would
    // otherwise never get re-linked - CS then finds no Info.ini ("no plugin version
    // found"), wipes, and recompiles every launch. This re-links only staged files
    // missing from the live dir; it's a cheap no-op when the cache is already whole.
    if (auto* prof = m_profileMgr->activeProfile()) {
        const QString cacheStaging = cacheStagingPath(prof);
        if (!cacheStaging.isEmpty()) {
            const bool hardlink =
                solero::AppConfig::instance().deployMode() == solero::DeployMode::HardLink;
            int failedRelink = 0;
            const int relinked = solero::assertShaderCacheDeployed(
                solero::AppConfig::instance().gameDir(), cacheStaging, hardlink,
                nullptr, &failedRelink);
            if (failedRelink > 0)
                statusBar()->showMessage(
                    QString("Couldn't restore %1 shader cache file(s) - Community "
                            "Shaders may recompile shaders on this launch.").arg(failedRelink));
            else if (relinked > 0)
                statusBar()->showMessage(
                    QString("Restored %1 shader cache file(s) into the game dir.").arg(relinked));
        }
    }

    const QString gameDir = solero::AppConfig::instance().gameDir();
    // Find a launch target in the game root, case-insensitively. Prefer the SKSE
    // loader so script-extender plugins load; fall back to the base game exe.
    auto findExeCI = [&](const QString& name) -> QString {
        if (QFile::exists(gameDir + "/" + name)) return gameDir + "/" + name;
        const auto matches = QDir(gameDir).entryList(QDir::Files);
        for (const QString& f : matches)
            if (f.compare(name, Qt::CaseInsensitive) == 0) return gameDir + "/" + f;
        return {};
    };
    const QString skse = findExeCI("skse64_loader.exe");
    if (skse.isEmpty()) {
        // SKSE's loader isn't deployed - required by virtually every modlist (and
        // launching the bare game would load no script-extender mods). Offer to
        // install it from Nexus rather than starting a broken session.
        statusBar()->showMessage("SKSE not found - it's needed to launch with script-extender mods.");
        ensureSkse();
        return;
    }

    // Steam DRM: launching the game's exe directly through Proton needs (a)
    // steam_appid.txt so steam_api knows the appid, and (b) the Steam CLIENT
    // running so steam_api can authenticate. Without the client, the game exits
    // instantly (diagnosed: "steam-runtime-steam-remote: Steam is not running").
    {
        QFile appid(gameDir + "/steam_appid.txt");
        if (!appid.exists() && appid.open(QIODevice::WriteOnly)) {
            appid.write("489830"); appid.close();
        }
    }
    auto steamRunning = [] {
        QProcess p; p.start("pgrep", {"-x", "steam"}); p.waitForFinished(2000);
        return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    };
    if (!steamRunning()) {
        statusBar()->showMessage("Starting Steam (needed for the game's DRM)\xe2\x80\xa6");
        qApp->processEvents();
        QProcess::startDetached("steam", {"-silent"});
        QElapsedTimer t; t.start();
        while (!steamRunning() && t.elapsed() < 30000) { QThread::msleep(400); qApp->processEvents(); }
        if (!steamRunning()) {
            QMessageBox::warning(this, "Play",
                "Steam must be running to launch the game (it handles Skyrim's DRM).\n"
                "Please start Steam, then click Play again.");
            return;
        }
        // Give the freshly-started client a moment to finish initializing.
        QElapsedTimer g; g.start();
        while (g.elapsed() < 5000) { QThread::msleep(200); qApp->processEvents(); }
    }

    solero::Executable exe;
    exe.name       = "Skyrim Special Edition (SKSE)";
    exe.binaryPath = skse;
    exe.workingDir = gameDir;
    exe.runtime    = solero::RuntimeType::Proton;          // launch through the Skyrim Proton prefix
    exe.winePrefix = QDir(gameDir + "/../..").canonicalPath() + "/compatdata/489830";
    // No VFS: files the game writes at runtime (e.g. Community Shaders' shader
    // cache) land in the real Data folder and stay there - they are not captured
    // or moved into Overwrite.

    // Lock the UI (MO2-style) while the game runs; ToolRunner blocks via an event
    // loop until the process exits, then we unlock.
    m_toolRunning = true;
    showRunLock(exe.name);
    statusBar()->showMessage("Launching " + exe.name + "\xe2\x80\xa6");
    QElapsedTimer runTimer; runTimer.start();
    QString outFolder, owDir;
    if (auto* p = m_profileMgr->activeProfile()) {
        if (!exe.outputModId.isEmpty()) outFolder = p->stagingFolderFor(exe.outputModId);
        owDir = solero::AppConfig::overwriteDir(p->name());
    }
    auto res = solero::ToolRunner::run(exe, gameDir, solero::AppConfig::instance().stagingDir(),
                                       outFolder, owDir);
    const qint64 ranMs = runTimer.elapsed();
    hideRunLock();
    m_toolRunning = false;

    // Always log the launch output for diagnosis.
    {
        QFile lg(solero::AppConfig::dataRoot() + "/play.log");
        if (lg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            lg.write(("exit=" + QString::number(res.launched) + " ranMs=" + QString::number(ranMs)
                      + "\n" + res.output).toUtf8());
        }
    }
    if (!res.launched) {
        QMessageBox::warning(this, "Play",
            looksHumanReadable(res.error) ? "The game failed to start: " + res.error
                : "The game failed to start. Make sure Steam is running, SKSE is installed and deployed, and the game path is correct in Settings.");
    } else if (ranMs < 8000) {
        // The game exited almost immediately - almost always a launch problem.
        QMessageBox::warning(this, "Play",
            "The game exited almost immediately - it likely failed to start "
            "(e.g. Steam DRM, a missing dependency, or a crashing plugin).\n\n"
            "See ~/.local/share/solero/play.log for the launch output.");
    } else {
        statusBar()->showMessage("Game closed.");
    }

    // Post-play shader-cache capture: if Solero manages the CS cache, move any
    // newly-compiled shaders out of the live game dir into the managed mod's
    // staging (scoped strictly to Data/ShaderCache). Best-effort; never blocks.
    if (auto* p = m_profileMgr->activeProfile()) {
        const QString key = solero::activeCacheKey(p->modList());
        QString cacheFolder = p->shaderCache().folderFor(key);
        // First play on a new CS version: managed but no folder yet for this key.
        // Create it now (named after the CS version, or the key as fallback) so the
        // freshly-compiled shaders are captured into this version's own cache.
        if (p->shaderCache().managed && cacheFolder.isEmpty()) {
            const solero::ModEntry* cs = p->modList().findCommunityShaders();
            const QString label = (cs && !cs->version.isEmpty()) ? cs->version : key;
            cacheFolder = solero::uniqueStagingFolder(
                solero::sanitizeStagingFolder(
                    p->name() + " - Shader Cache (" + label + ")"),
                takenStagingFolders(p));
            QDir().mkpath(solero::AppConfig::instance().stagingDir()
                          + "/" + cacheFolder + "/Data");
            p->shaderCache().folders.insert(key, cacheFolder);
            p->save();
        }
        const QString cacheStaging = cacheFolder.isEmpty()
            ? QString()
            : solero::AppConfig::instance().stagingDir() + "/" + cacheFolder;
        if (!cacheStaging.isEmpty()) {
            QStringList moved;
            solero::captureShaderCache(gameDir, cacheStaging, &moved);
            if (!moved.isEmpty() && m_deployed) {
                // capture MOVED the new shaders out of the live game dir into the
                // managed cache's staging, leaving the deployed game dir incomplete.
                // Re-link them straight back in (and record them in the deploy
                // record) so the live dir stays whole and the deploy state stays
                // clean - no more stuck "Redeploy" after every Play.
                solero::Linker linker(solero::AppConfig::instance().deployMode());
                solero::DeployRecord rec =
                    solero::DeployRecord::loadFromFile(solero::DeployEngine::recordPath(gameDir));
                bool anyFail = false;
                for (const QString& relPath : moved) {
                    const QString src = cacheStaging + "/" + relPath;
                    const QString dst = gameDir + "/" + relPath;
                    if (linker.deploy(src, dst))
                        rec.add(relPath, QStringLiteral("__shadercache__"));
                    else
                        anyFail = true;
                }
                rec.saveToFile(solero::DeployEngine::recordPath(gameDir));
                if (anyFail) {
                    // A re-link failed: the live dir is now missing a captured
                    // shader, so fall back to forcing a redeploy on next Play.
                    m_deployDirty = true;
                }
                updateDeployButton();
                statusBar()->showMessage(
                    QString("Captured %1 shader cache file(s).").arg(moved.size()));
            }
        }
    }

    // Refresh cached scans after the session (mirrors the post-run refresh in
    // onRunTool) so the mod list and plugin view reflect the current on-disk state.
    m_modListView->invalidateModCache();
    m_rightPane->invalidateModPluginCache();
    if (auto* p = m_profileMgr->activeProfile()) { m_rightPane->refreshPlugins(p); m_modListView->setProfile(p); }
    updatePluginNotice();
}

void MainWindow::onEditTool(const QString& id) {
    auto& tools = activeTools();
    for (auto& t : tools) if (t.id == id) {
        solero::ExecutableDialog dlg(t, this);
        dlg.setOutputModChoices(modChoices(), t.outputModId);
        if (dlg.exec() == QDialog::Accepted) {
            auto e = dlg.result();
            e.id = id;
            t = e;
            saveActiveTools();
            rebuildToolsMenu();
        }
        break;
    }
}

QString MainWindow::modNameAnywhere(const QString& id) const {
    if (auto* a = m_profileMgr->activeProfile())
        if (const auto* m = a->modList().findById(id)) return m->name;
    for (const QString& name : m_profileMgr->profileNames()) {
        solero::ModList ml = solero::ModList::loadFromFile(profilesRoot() + "/" + name + "/modlist.json");
        if (const auto* m = ml.findById(id)) return m->name;
    }
    return {};
}

void MainWindow::removeModEverywhere(const QString& id) {
    // Resolve the on-disk staging folder name (name-based after migration) from
    // whichever profile holds the mod, *before* the entry is removed. Staged
    // files are profile-independent, so we delete them once at the end.
    // A Keep-Both mod owns one staging folder per version variant; collect them all
    // (plus the mirror folder) so deleting the mod removes every version's files.
    QStringList folders;
    auto captureFolders = [&](const solero::ModEntry* e) {
        if (!e) return;
        auto add = [&](const QString& f) {
            if (!f.isEmpty() && !folders.contains(f)) folders << f;
        };
        add(e->stagingFolder);
        for (const auto& v : e->variants) add(v.stagingFolder);
    };
    // If any holding profile fails to persist its modlist after dropping the mod,
    // we must not delete the staging - the on-disk profile still references it, and
    // wiping the files would leave that profile pointing at missing files.
    bool allSaved = true;
    auto* active = m_profileMgr->activeProfile();
    for (const QString& name : m_profileMgr->profileNames()) {
        if (active && name == active->name()) {
            captureFolders(active->modList().findById(id));
            active->modList().remove(id);
            if (!checkSave(active->save())) allSaved = false;
        } else {
            QString path = profilesRoot() + "/" + name + "/modlist.json";
            solero::ModList ml = solero::ModList::loadFromFile(path);
            captureFolders(ml.findById(id));
            ml.remove(id);
            if (!ml.saveToFile(path)) {
                allSaved = false;
                qCWarning(lcProfile) << "removeModEverywhere: failed to save modlist for profile"
                                     << name << "-- keeping staging for" << id;
            }
        }
    }
    if (!allSaved) {
        statusBar()->showMessage(
            "Couldn't fully remove the mod from every profile - its staged files were "
            "kept so no profile is left pointing at missing files. Try again.", 8000);
        return;
    }
    if (folders.isEmpty()) folders << id; // pre-migration mods (or unknown) used the id
    const QString stagingDir = solero::AppConfig::instance().stagingDir();
    bool anyLeftBehind = false;
    for (const QString& folder : folders) {
        QDir dir(stagingDir + "/" + folder);
        if (dir.exists() && !dir.removeRecursively()) anyLeftBehind = true;
    }
    if (anyLeftBehind)
        statusBar()->showMessage(
            "Some staged files couldn't be deleted and may remain in the staging "
            "directory - delete them manually if you need the disk space.", 8000);
}

bool MainWindow::checkSave(bool ok) {
    if (!ok) reportSaveFailure();
    return ok;
}

void MainWindow::reportSaveFailure() {
    // Plain-language: what happened, why it matters, what to do. 8s so it doesn't
    // vanish before the user reads it.
    statusBar()->showMessage(
        "Could not save the profile - check disk space and permissions. "
        "Your last change may not survive a restart.", 8000);
}

void MainWindow::onRemoveTool(const QString& id) {
    auto* profile = m_profileMgr->activeProfile();

    // Find the tool's Executable and its display name.
    QString toolName = id;
    QStringList candidateIds;
    for (const auto& exe : activeTools()) {
        if (exe.id != id) continue;
        toolName = exe.name;
        if (!exe.outputModId.isEmpty()) candidateIds << exe.outputModId;
        for (const auto& a : exe.extraActions)
            if (!a.outputModId.isEmpty()) candidateIds << a.outputModId;
        break;
    }

    // DynDOLOD also installs a DynDOLOD Resources mod (named from its archive,
    // e.g. "dyndolod-resources"), so match loosely on the name.
    if (id == "dyndolod" && profile) {
        for (const auto& m : profile->modList()) {
            const QString n = m.name.toLower();
            if (m.type == solero::EntryType::Mod
                && n.contains("dyndolod") && n.contains("resource"))
                candidateIds << m.id;
        }
    }

    // De-duplicate and keep only ids that resolve to an existing mod - searching
    // all profiles, since an output mod may live in a non-active profile.
    QStringList modIds;
    QStringList modNames;
    for (const QString& mid : candidateIds) {
        if (modIds.contains(mid)) continue;
        QString name = modNameAnywhere(mid);
        if (name.isEmpty()) continue;
        modIds   << mid;
        modNames << name;
    }

    QList<QCheckBox*> boxes;
    if (modIds.isEmpty()) {
        auto ret = QMessageBox::question(this, "Remove Tool",
            QString("Remove tool '%1'? (The downloaded files stay on disk.)").arg(toolName),
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    } else {
        QDialog dlg(this);
        dlg.setWindowTitle("Remove Tool");
        auto* lay = new QVBoxLayout(&dlg);
        lay->addWidget(new QLabel(QString("Remove tool '%1'?").arg(toolName), &dlg));
        for (const QString& name : modNames) {
            auto* cb = new QCheckBox(QString("Also remove mod: \"%1\"").arg(name), &dlg);
            cb->setChecked(true);
            boxes << cb;
            lay->addWidget(cb);
        }
        auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(btns);
        if (dlg.exec() != QDialog::Accepted) return;
    }

    auto& tools = activeTools();
    for (int i = 0; i < tools.size(); ++i)
        if (tools[i].id == id) { tools.removeAt(i); break; }
    saveActiveTools();

    bool removedAny = false;
    for (int i = 0; i < modIds.size(); ++i) {
        if (i < boxes.size() && !boxes[i]->isChecked()) continue;
        removeModEverywhere(modIds[i]);   // deletes staging + removes from whichever profile holds it
        removedAny = true;
    }
    if (removedAny && profile) {
        m_modListView->setProfile(profile);
    }
    rebuildToolsMenu();
}
