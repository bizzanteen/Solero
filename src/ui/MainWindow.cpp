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
#include "install/ModInstaller.h"
#include "import/Mo2Importer.h"
#include "ui/FomodWizard.h"
#include "ui/ProgressModal.h"
#include "ui/ToolSetupWizard.h"
#include "ui/ExecutableDialog.h"
#include "ui/ToolsManagerDialog.h"
#include "tools/ToolRunner.h"
#include "tools/ToolCatalog.h"
#include "fomod/FomodEngine.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTabWidget>
#include <QSplitter>
#include <QComboBox>
#include <QToolBar>
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QDialog>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QStandardPaths>
#include <QMessageBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QIcon>
#include <QStatusBar>
#include <QTimer>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QUuid>
#include <QDesktopServices>
#include <QUrl>
#include <memory>

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

    setupToolbar();
    setupCentralWidget();
    statusBar()->showMessage("Ready");

    switchProfile(m_profileMgr->profileNames().first());
    refreshDeployState(); // reflect any existing deployment from a previous run
}

MainWindow::~MainWindow() {
    delete m_profileMgr;
    delete m_txLog;
}

void MainWindow::handleNxmUrl(const QString& url) {
    raise(); activateWindow();
    if (!solero::AppConfig::instance().isConfigured()) {
        QMessageBox::warning(this, "Nexus Download", "Configure the game first (\xe2\x9a\x99 Settings).");
        return;
    }
    auto link = solero::NxmHandler::parse(url);
    if (!link.valid) { QMessageBox::warning(this, "Nexus Download", "Couldn't understand that Nexus link:\n" + url); return; }
    statusBar()->showMessage("Resolving Nexus download\xe2\x80\xa6"); qApp->processEvents();
    QString cdn = solero::NxmHandler::resolveDownloadUrl(link);
    if (cdn.isEmpty()) {
        QMessageBox::warning(this, "Nexus Download",
            "Could not resolve the download. The link may have expired, or a Nexus Premium API key may be required for this file.");
        return;
    }
    QString fn = solero::NxmHandler::fileName(link);
    if (fn.isEmpty()) fn = "nexus-" + link.modId + "-" + link.fileId + ".archive";
    m_downloads->enqueue(cdn, fn, solero::AppConfig::instance().downloadsDir());
    // Show the Downloads tab so the user sees progress.
    m_rightPane->showDownloadsTab();
    statusBar()->showMessage("Downloading " + fn + "\xe2\x80\xa6");
}

QString MainWindow::profilesRoot() const {
    return solero::AppConfig::dataRoot() + "/profiles";
}

QString MainWindow::txLogPath() const {
    return solero::AppConfig::dataRoot() + "/ai-transactions.json";
}

void MainWindow::setupToolbar() {
    auto* tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setIconSize({24, 24});

    // Logo (emoji fallback, SVG not available)
    QLabel* logo = new QLabel("☀", tb);
    logo->setStyleSheet("font-size: 20px; padding: 4px;");
    tb->addWidget(logo);
    tb->addSeparator();

    // Profile selector
    tb->addWidget(new QLabel("Profile: "));
    m_profileCombo = new QComboBox(tb);
    m_profileCombo->setMinimumWidth(150);
    refreshProfileCombo();
    connect(m_profileCombo, &QComboBox::currentTextChanged,
            this, &MainWindow::switchProfile);
    tb->addWidget(m_profileCombo);

    // Profile management button
    auto* profileMenuBtn = new QToolButton(tb);
    profileMenuBtn->setText("\xe2\x9a\x99");
    auto* profileMenu = new QMenu(profileMenuBtn);
    profileMenu->addAction("New Profile...", this, &MainWindow::onNewProfile);
    profileMenu->addAction("Delete Current Profile", this, &MainWindow::onDeleteProfile);
    profileMenu->addSeparator();
    profileMenu->addAction("Import MO2 Profile...", this, &MainWindow::onImportMo2);
    profileMenuBtn->setMenu(profileMenu);
    profileMenuBtn->setPopupMode(QToolButton::InstantPopup);
    tb->addWidget(profileMenuBtn);
    tb->addSeparator();

    // Install Mod action
    tb->addAction("Install Mod...", this, &MainWindow::onInstallMod);
    tb->addSeparator();

    // Tools dropdown
    m_toolsBtn = new QToolButton(tb);
    m_toolsBtn->setText("Tools \xe2\x96\xbe");
    m_toolsBtn->setPopupMode(QToolButton::InstantPopup);
    m_toolsMenu = new QMenu(m_toolsBtn);
    m_toolsBtn->setMenu(m_toolsMenu);
    tb->addWidget(m_toolsBtn);

    // BethINI (modal window)
    tb->addAction("BethINI", this, &MainWindow::onOpenBethini);
    tb->addSeparator();

    // Deploy toggle
    m_deployAction = tb->addAction("\xe2\x9c\x97 Not Deployed", this, &MainWindow::onDeployToggle);
    m_deployAction->setToolTip("Click to deploy mods to game directory");
    tb->addSeparator();

    // Play (launch Skyrim via Steam)
    tb->addAction("\xe2\x96\xb6 Play", this, &MainWindow::onPlay);
    tb->addSeparator();

    // Game settings
    tb->addAction("\xe2\x9a\x99 Settings", this, [this]{
        solero::SettingsDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted)
            statusBar()->showMessage("Settings updated.");
    });

    rebuildToolsMenu();
}

void MainWindow::setupCentralWidget() {
    auto* outer = new QSplitter(Qt::Vertical, this);
    m_splitter = new QSplitter(Qt::Horizontal, outer);

    m_modListView = new solero::ModListView(m_splitter);
    m_rightPane   = new solero::RightPane(m_splitter);
    m_bethiniWindow = new solero::BethiniWindow(this); // shown as a top-level modal window
    m_bethiniWindow->setWindowFlag(Qt::Window, true);  // not an in-canvas child of MainWindow
    m_bethiniWindow->hide();                           // stays hidden until the BethINI button

    m_splitter->addWidget(m_modListView);
    m_splitter->addWidget(m_rightPane);
    m_splitter->setSizes({640, 640});

    m_downloads = new solero::DownloadManager(this);
    connect(m_downloads, &solero::DownloadManager::progress, this,
        [this](const QString& fn, qint64 r, qint64 t){
            m_rightPane->downloadsTab()->setDownloadProgress(fn, r, t);
        });
    connect(m_downloads, &solero::DownloadManager::finished, this,
        [this](const QString& fn, const QString& path, bool ok, const QString& err){
            Q_UNUSED(path);
            m_rightPane->downloadsTab()->setDownloadProgress(fn, ok ? 1 : 0, ok ? 1 : 0); // mark complete
            m_rightPane->downloadsTab()->refresh();
            statusBar()->showMessage(ok ? ("Downloaded: " + fn) : ("Download failed: " + fn + " - " + err));
        });

    connect(m_rightPane->downloadsTab(), &solero::DownloadsTab::installRequested,
            this, &MainWindow::installFromArchive);
    connect(m_rightPane->downloadsTab(), &solero::DownloadsTab::cancelRequested,
            this, [this](const QString& fn){ m_downloads->cancel(fn); });
    connect(m_modListView, &solero::ModListView::modsSelected,
            m_rightPane, &solero::RightPane::onSelectionChanged);
    connect(m_modListView, &solero::ModListView::reinstallRequested,
            this, &MainWindow::onReinstallMod);
    connect(m_modListView, &solero::ModListView::modsChanged,
            this, &MainWindow::onModsChanged);
    connect(m_modListView, &solero::ModListView::modActivated,
            m_rightPane, &solero::RightPane::showDataFor);

    m_bottomPanel = new solero::BottomPanel(outer);
    outer->addWidget(m_splitter);
    outer->addWidget(m_bottomPanel);
    outer->setSizes({580, 200});
    setCentralWidget(outer);
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
        engine.undeploy(solero::AppConfig::instance().gameDir(),
                        [&](int d, int t){ if (prog) { prog->setProgress(d, t); prog->pump(); } });
    }

    if (prog) prog->setMessage("Loading " + name + "...");
    auto* profile = m_profileMgr->loadProfile(name);
    seedProfileInis(profile);
    m_ipcServer->setActiveProfile(profile);
    m_modListView->setProfile(profile);
    m_rightPane->setProfile(profile);
    m_bottomPanel->setProfile(profile);
    m_bethiniWindow->setProfile(profile);
    // Self-review fix: load previously-computed ConflictIndex if it exists
    QString conflictPath = solero::DeployEngine::conflictIndexPath(profile->path());
    if (QFile::exists(conflictPath))
        m_rightPane->setConflictIndex(solero::ConflictIndex::loadFromFile(conflictPath));
    setWindowTitle(QString("Solero - %1").arg(name));

    // Deploy the incoming profile if the previous one was deployed.
    if (wasDeployed && solero::AppConfig::instance().isConfigured()) {
        if (prog) prog->setMessage("Deploying " + name + "...");
        statusBar()->showMessage("Switching profile - deploying " + name + "...");
        qApp->processEvents();
        solero::DeployEngine engine(solero::AppConfig::instance().gameDir(),
                                    solero::AppConfig::instance().stagingDir());
        engine.setUserlistPath(profile->lootUserlistPath());
        auto result = engine.deploy(*profile, m_deployMode,
                                    [&](int d, int t){ if (prog) { prog->setProgress(d, t); prog->pump(); } });
        m_deployed = result.success;
        m_deployDirty = false;
        if (result.success) {
            m_rightPane->setConflictIndex(result.conflicts);
            m_rightPane->setProfile(profile);
        }
        updateDeployButton();
        statusBar()->showMessage(QString("Switched to '%1' (redeployed %2 files).").arg(name).arg(result.filesDeployed));
    } else {
        statusBar()->showMessage(QString("Loaded profile: %1").arg(name));
    }
    if (prog) prog->close();
    updatePluginNotice();
}

void MainWindow::refreshProfileCombo() {
    QSignalBlocker blocker(m_profileCombo);
    m_profileCombo->clear();
    m_profileCombo->addItems(m_profileMgr->profileNames());
}

bool MainWindow::deployCurrent() {
    if (m_toolRunning) {
        statusBar()->showMessage("A tool is running - please wait for it to finish.");
        return false;
    }
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) { statusBar()->showMessage("No active profile."); return false; }

    statusBar()->showMessage("Deploying...");
    qApp->processEvents();

    solero::ProgressModal prog(this, "Deploy", "Deploying mods + sorting plugins with LOOT...");
    prog.show(); prog.pump();

    solero::DeployEngine engine(
        solero::AppConfig::instance().gameDir(),
        solero::AppConfig::instance().stagingDir());
    engine.setUserlistPath(profile->lootUserlistPath());
    auto result = engine.deploy(*profile, m_deployMode, [&](int d, int t){ prog.setProgress(d, t); prog.pump(); });

    prog.close();

    if (!result.success) {
        // Partial deploy: some files failed but the rest are in place. Warn,
        // don't pretend success, but keep whatever did deploy.
        QMessageBox::warning(this, "Deploy Incomplete", result.errorMessage);
    }
    if (!result.warning.isEmpty())
        QMessageBox::information(this, "Deploy Notice", result.warning);

    m_deployed = result.success;
    m_deployDirty = false;
    statusBar()->showMessage(
        QString("Deployed %1 files. %2 conflicts. Plugins sorted by LOOT.")
            .arg(result.filesDeployed)
            .arg(result.conflicts.conflictedPaths().size()));
    m_rightPane->setConflictIndex(result.conflicts);
    m_rightPane->setProfile(profile); // refresh plugin list - LOOT may have reordered it
    emit conflictsUpdated(result.conflicts);
    updateDeployButton();
    updatePluginNotice();
    return result.success;
}

void MainWindow::onDeployToggle() {
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

    if (!m_deployed || m_deployDirty) {
        if (!deployCurrent()) return;
    } else {
        auto ret = QMessageBox::question(this, "UnDeploy",
            "Remove all deployed mod links? Staged mods will not be affected.",
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;

        solero::ProgressModal prog(this, "Deploy", "Undeploying...");
        prog.show(); prog.pump();

        solero::DeployEngine engine(
            solero::AppConfig::instance().gameDir(),
            solero::AppConfig::instance().stagingDir());
        engine.undeploy(solero::AppConfig::instance().gameDir(), [&](int d, int t){ prog.setProgress(d, t); prog.pump(); });

        prog.close();
        m_deployed = false;
        m_deployDirty = false;
        if (auto* p = m_profileMgr->activeProfile()) m_rightPane->refreshPlugins(p);
        statusBar()->showMessage("Undeployed.");
    }

    updateDeployButton();
    updatePluginNotice();
}

void MainWindow::updateDeployButton() {
    if (!m_deployAction) return;
    if (m_deployed && m_deployDirty) {
        m_deployAction->setText("\xe2\x9a\xa0 Redeploy");   // ⚠
        m_deployAction->setToolTip("Mod changes since last deploy - click to redeploy");
    } else if (m_deployed) {
        m_deployAction->setText("\xe2\x9c\x93 Deployed");
        m_deployAction->setToolTip("Mods are deployed - click to undeploy");
    } else {
        m_deployAction->setText("\xe2\x9c\x97 Not Deployed");
        m_deployAction->setToolTip("Click to deploy mods to game directory");
    }
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

void MainWindow::onModsChanged() {
    auto* profile = m_profileMgr->activeProfile();
    if (profile) m_rightPane->refreshPlugins(profile);  // plugins follow enabled mods
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    updatePluginNotice();
}

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
            result = solero::ModInstaller::stageSimple(prep, staging, QString(),
                [&](int pct){ stageProg.setProgress(pct, 100); });
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
                        && ciExists(solero::AppConfig::instance().stagingDir() + "/" + m.id + "/Data", file))
                        return true;
                return false;
            });
            solero::FomodWizard wizard(&engine, prep.extractDir, this);
            if (wizard.exec() != QDialog::Accepted) { statusBar()->showMessage("Install cancelled."); return; }
            solero::ProgressModal stageProg(this, "Install", "Installing files...");
            stageProg.show(); stageProg.pump();
            result = solero::ModInstaller::stageFomod(prep, staging, wizard.result(), QString(),
                [&](int pct){ stageProg.setProgress(pct, 100); });
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
        result = solero::ModInstaller::stageSimple(prep, staging, QString(),
            [&](int pct){ stageProg.setProgress(pct, 100); });
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

    solero::ModEntry mod;
    mod.type = solero::EntryType::Mod;
    mod.id = result.modId;
    mod.name = result.modName;
    mod.enabled = true;
    mod.hasFomodChoices = !choiceLog.isEmpty();
    mod.sourceArchive = archive;
    profile->modList().append(mod);
    profile->save();

    m_modListView->setProfile(profile);
    if (auto* p = m_profileMgr->activeProfile()) m_rightPane->refreshPlugins(p);
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    m_rightPane->downloadsTab()->refresh();
    updatePluginNotice();
    statusBar()->showMessage(QString("Installed: %1").arg(result.modName));
}

void MainWindow::onReinstallMod(const QString& modId) {
    auto* profile = m_profileMgr->activeProfile();
    if (!profile) return;
    // Find the existing entry.
    solero::ModEntry* existing = profile->modList().findById(modId);
    if (!existing) return;

    QString archive = existing->sourceArchive;
    if (archive.isEmpty() || !QFile::exists(archive))
        archive = QFileDialog::getOpenFileName(this, "Reinstall: choose the mod archive",
            solero::AppConfig::instance().downloadsDir().isEmpty() ? QDir::homePath()
                : solero::AppConfig::instance().downloadsDir(),
            "Mod archives (*.zip *.7z *.rar *.tar *.gz);;All files (*)");
    if (archive.isEmpty()) return;

    statusBar()->showMessage("Preparing...");
    qApp->processEvents();

    auto extractProg = std::make_unique<solero::ProgressModal>(this, "Reinstall", "Extracting archive...");
    extractProg->show(); extractProg->pump();

    auto prep = solero::ModInstaller::prepare(archive, [&](int pct){ extractProg->setProgress(pct, 100); });
    if (!prep.ok) { extractProg->close(); QMessageBox::critical(this, "Reinstall Failed", prep.errorMessage); return; }

    const QString staging = solero::AppConfig::instance().stagingDir();
    solero::InstallResult result;
    QJsonArray choiceLog;

    if (prep.layout.isFomod && !prep.fomodConfigPath.isEmpty()) {
        solero::FomodEngine engine;
        if (!engine.load(prep.fomodConfigPath)) {
            extractProg->close();
            solero::ProgressModal stageProg(this, "Reinstall", "Installing files...");
            stageProg.show(); stageProg.pump();
            result = solero::ModInstaller::stageSimple(prep, staging, modId,
                [&](int pct){ stageProg.setProgress(pct, 100); });
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
                        && ciExists(solero::AppConfig::instance().stagingDir() + "/" + m.id + "/Data", file))
                        return true;
                return false;
            });
            solero::FomodWizard wizard(&engine, prep.extractDir, this);
            if (wizard.exec() != QDialog::Accepted) { statusBar()->showMessage("Reinstall cancelled."); return; }
            solero::ProgressModal stageProg(this, "Reinstall", "Installing files...");
            stageProg.show(); stageProg.pump();
            result = solero::ModInstaller::stageFomod(prep, staging, wizard.result(), modId,
                [&](int pct){ stageProg.setProgress(pct, 100); });
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
            [&](int pct){ stageProg.setProgress(pct, 100); });
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
    existing->hasFomodChoices = !choiceLog.isEmpty();
    existing->sourceArchive = archive;
    profile->save();
    m_modListView->setProfile(profile);
    if (auto* p = m_profileMgr->activeProfile()) m_rightPane->refreshPlugins(p);
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
    updatePluginNotice();
    statusBar()->showMessage("Reinstalled: " + existing->name);
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
    updateDeployButton();
    updatePluginNotice();
}

void MainWindow::onNewProfile() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Profile", "Profile name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    if (!m_profileMgr->createProfile(name.trimmed())) {
        QMessageBox::warning(this, "Error", QString("Profile '%1' already exists.").arg(name));
        return;
    }
    refreshProfileCombo();
    m_profileCombo->setCurrentText(name.trimmed());
}

void MainWindow::onDeleteProfile() {
    QString current = m_profileCombo->currentText();
    if (current.isEmpty()) return;
    if (m_profileMgr->profileNames().count() <= 1) {
        QMessageBox::warning(this, "Error", "Cannot delete the last profile.");
        return;
    }
    auto ret = QMessageBox::question(this, "Delete Profile",
        QString("Delete profile '%1'? This cannot be undone.").arg(current),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    m_profileMgr->deleteProfile(current);
    refreshProfileCombo();
    switchProfile(m_profileMgr->profileNames().first());
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

    statusBar()->showMessage("Importing MO2 profile...");
    qApp->processEvents();
    auto r = solero::Mo2Importer::importProfile(profileDir, modsDir,
        solero::AppConfig::instance().stagingDir(), *m_profileMgr, name.trimmed(), symlink);
    if (!r.success) { QMessageBox::critical(this, "Import Failed", r.errorMessage); return; }

    refreshProfileCombo();
    m_profileCombo->setCurrentText(r.profileName);
    statusBar()->showMessage(QString("Imported '%1' - %2 mods staged.").arg(r.profileName).arg(r.modsStaged));
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        auto* app = static_cast<Application*>(qApp);
        if (event->key() == Qt::Key_Equal || event->key() == Qt::Key_Plus) { app->setZoomFactor(app->zoomFactor() + 0.1); return; }
        if (event->key() == Qt::Key_Minus)  { app->setZoomFactor(app->zoomFactor() - 0.1); return; }
        if (event->key() == Qt::Key_0)      { app->setZoomFactor(1.0); return; }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::onZoomIn()    { static_cast<Application*>(qApp)->setZoomFactor(static_cast<Application*>(qApp)->zoomFactor() + 0.1); }
void MainWindow::onZoomOut()   { static_cast<Application*>(qApp)->setZoomFactor(static_cast<Application*>(qApp)->zoomFactor() - 0.1); }
void MainWindow::onZoomReset() { static_cast<Application*>(qApp)->setZoomFactor(1.0); }

void MainWindow::onRunTool(const solero::Executable& exe) {
    if (m_toolRunning) {
        statusBar()->showMessage("A tool is running - please wait for it to finish.");
        return;
    }
    if (!solero::AppConfig::instance().isConfigured()) {
        QMessageBox::warning(this, "Tool", "Configure the game first (Game Settings\xe2\x80\xa6).");
        return;
    }
    // Tools operate on the live Data/ folder, so the modlist must be deployed & current.
    if (!m_deployed || m_deployDirty) {
        QMessageBox box(this);
        box.setWindowTitle("Deploy required");
        box.setIcon(QMessageBox::Warning);
        box.setText("Modlist must be deployed first.");
        box.setInformativeText("Tools run against the game's Data folder, so your mods need to be deployed"
                               + QString(m_deployDirty ? " (you have undeployed changes)." : "."));
        QAbstractButton* deployBtn = box.addButton("Deploy", QMessageBox::AcceptRole);
        box.addButton("Cancel", QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() != deployBtn) return;
        if (!deployCurrent()) { QMessageBox::critical(this, "Deploy Failed", "Could not deploy - see status bar."); return; }
    }

    // Lock the UI (MO2-style) while the tool runs. m_toolRunning stays true for
    // the whole run - even if the user dismisses the overlay via "Unlock Solero" -
    // so the re-entrancy guards remain active until the run actually finishes.
    m_toolRunning = true;
    showRunLock(exe.name);
    auto res = solero::ToolRunner::run(exe, solero::AppConfig::instance().gameDir(),
                                       solero::AppConfig::instance().stagingDir());
    hideRunLock();
    m_toolRunning = false;

    if (!res.launched) {
        QMessageBox::warning(this, "Tool", res.error.isEmpty() ? ("Failed to launch " + exe.name) : res.error);
    } else if (!res.output.isEmpty()) {
        // Surface tool output (helps diagnose e.g. wine 'Not implemented').
        statusBar()->showMessage(exe.name + " finished.");
    }
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

    QDir().mkpath(solero::AppConfig::instance().stagingDir() + "/" + mod.id + "/Data");

    profile->modList().append(mod);
    profile->save();
    m_modListView->setProfile(profile);

    return mod.id;
}

void MainWindow::onAddTool2() {
    solero::ToolSetupWizard dlg(this, m_toolStore);
    dlg.setModChoices(modChoices());
    connect(&dlg, &solero::ToolSetupWizard::installModRequested,
            this, &MainWindow::installFromArchive);
    dlg.exec();
    // Wire output mods for any set-up tool that produces output.
    for (auto exe : m_toolStore->tools()) {                 // copy
        const auto* preset = solero::ToolCatalog::byId(exe.id);
        if (!preset) continue;
        bool changed = false;
        if (preset->producesOutput && exe.outputModId.isEmpty()) {
            exe.outputModId = ensureOutputMod(preset->outputModName);
            exe.isCapturingOutput = true;
            changed = true;
        }
        // secondary actions: match by index to the preset's extraActions
        for (int i = 0; i < exe.extraActions.size() && i < preset->extraActions.size(); ++i) {
            if (exe.extraActions[i].outputModId.isEmpty()
                && !preset->extraActions[i].outputModName.isEmpty()) {
                exe.extraActions[i].outputModId = ensureOutputMod(preset->extraActions[i].outputModName);
                changed = true;
            }
        }
        if (changed) { m_toolStore->update(exe); m_toolStore->save(); }
    }
    rebuildToolsMenu();
}

void MainWindow::rebuildToolsMenu() {
    if (!m_toolsMenu) return;
    m_toolsMenu->clear();
    const auto& tools = m_toolStore->tools();
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
    // Use real icons (in the icon column) so these align with the tool entries above.
    m_toolsMenu->addAction(QIcon::fromTheme("list-add"), "Add tool\xe2\x80\xa6", this, &MainWindow::onAddTool2);
    m_toolsMenu->addAction(QIcon::fromTheme("configure", QIcon::fromTheme("settings-configure")),
                           "Manage tools\xe2\x80\xa6", this, &MainWindow::onManageTools);
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
    solero::ToolsManagerDialog dlg(m_toolStore, this);
    connect(&dlg, &solero::ToolsManagerDialog::addToolRequested, this, [this, &dlg]{ onAddTool2(); dlg.refresh(); });
    connect(&dlg, &solero::ToolsManagerDialog::editToolRequested, this, [this, &dlg](const QString& id){ onEditTool(id); dlg.refresh(); });
    connect(&dlg, &solero::ToolsManagerDialog::removeToolRequested, this, [this, &dlg](const QString& id){ onRemoveTool(id); dlg.refresh(); });
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

void MainWindow::onPlay() {
    // Launch Skyrim SE through Steam.
    QDesktopServices::openUrl(QUrl("steam://rungameid/489830"));
    statusBar()->showMessage("Launching Skyrim via Steam\xe2\x80\xa6");
}

void MainWindow::onEditTool(const QString& id) {
    for (const auto& t : m_toolStore->tools()) if (t.id == id) {
        solero::ExecutableDialog dlg(t, this);
        dlg.setOutputModChoices(modChoices(), t.outputModId);
        if (dlg.exec() == QDialog::Accepted) {
            auto e = dlg.result();
            e.id = id;
            m_toolStore->update(e);
            m_toolStore->save();
            rebuildToolsMenu();
        }
        break;
    }
}

void MainWindow::onRemoveTool(const QString& id) {
    auto* profile = m_profileMgr->activeProfile();

    // Find the tool's Executable and its display name.
    QString toolName = id;
    QStringList candidateIds;
    for (const auto& exe : m_toolStore->tools()) {
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

    // De-duplicate and keep only ids that resolve to an existing mod.
    QStringList modIds;
    QStringList modNames;
    for (const QString& mid : candidateIds) {
        if (modIds.contains(mid)) continue;
        const solero::ModEntry* m = profile ? profile->modList().findById(mid) : nullptr;
        if (!m) continue;
        modIds   << mid;
        modNames << m->name;
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

    m_toolStore->remove(id);
    m_toolStore->save();

    bool removedAny = false;
    for (int i = 0; i < modIds.size(); ++i) {
        if (i < boxes.size() && !boxes[i]->isChecked()) continue;
        if (!profile) break;
        profile->modList().remove(modIds[i]);
        QDir(solero::AppConfig::instance().stagingDir() + "/" + modIds[i]).removeRecursively();
        removedAny = true;
    }
    if (removedAny && profile) {
        profile->save();
        m_modListView->setProfile(profile);
    }
    rebuildToolsMenu();
}
