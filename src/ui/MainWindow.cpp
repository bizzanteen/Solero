#include "MainWindow.h"
#include "ModListView.h"
#include "RightPane.h"
#include "DownloadsTab.h"
#include "BottomPanel.h"
#include "SetupWizard.h"
#include "bethini/BethiniWindow.h"
#include "app/Application.h"
#include "core/AppConfig.h"
#include "install/ModInstaller.h"
#include "import/Mo2Importer.h"
#include "ui/FomodWizard.h"
#include "ui/ProgressModal.h"
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
#include <QKeyEvent>
#include <QStandardPaths>
#include <QMessageBox>
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

    // Deploy toggle
    m_deployAction = tb->addAction("\xe2\x9c\x97 Not Deployed", this, &MainWindow::onDeployToggle);
    m_deployAction->setToolTip("Click to deploy mods to game directory");
    tb->addSeparator();

    // AI changes badge
    m_aiChangesLabel = new QLabel("AI: 0 changes", tb);
    tb->addWidget(m_aiChangesLabel);
    tb->addSeparator();

    // BethINI editor (opens as a tab)
    tb->addAction("BethINI", this, &MainWindow::onOpenBethini);

    auto* toolsBtn = new QToolButton(tb);
    toolsBtn->setText("Tools \xe2\x96\xbe");
    toolsBtn->setPopupMode(QToolButton::InstantPopup);
    m_toolsBtn = toolsBtn;
    tb->addWidget(toolsBtn);
    rebuildToolsMenu();

    // Game settings
    tb->addAction("Game Settings...", this, [this]{
        solero::SetupWizard wizard(this);
        if (wizard.exec() == QDialog::Accepted)
            statusBar()->showMessage("Game settings updated.");
    });
}

void MainWindow::onOpenBethini() {
    if (m_centralTabs) m_centralTabs->setCurrentWidget(m_bethiniWindow);
}

void MainWindow::setupCentralWidget() {
    auto* outer = new QSplitter(Qt::Vertical, this);

    m_splitter = new QSplitter(Qt::Horizontal, outer);
    m_modListView    = new solero::ModListView(m_splitter);
    m_rightPane = new solero::RightPane(m_splitter);
    connect(m_rightPane->downloadsTab(), &solero::DownloadsTab::installRequested,
            this, &MainWindow::installFromArchive);
    m_splitter->addWidget(m_modListView);
    m_splitter->addWidget(m_rightPane);
    connect(m_modListView, &solero::ModListView::modsSelected,
            m_rightPane,   &solero::RightPane::onSelectionChanged);
    connect(m_modListView, &solero::ModListView::reinstallRequested,
            this, &MainWindow::onReinstallMod);
    connect(m_modListView, &solero::ModListView::modsChanged,
            this, &MainWindow::onModsChanged);
    connect(m_modListView, &solero::ModListView::modActivated,
            m_rightPane,   &solero::RightPane::showDataFor);
    m_splitter->setSizes({640, 640});

    m_bottomPanel = new solero::BottomPanel(outer);
    outer->addWidget(m_splitter);
    outer->addWidget(m_bottomPanel);
    outer->setSizes({580, 200});

    // Central area is tabbed: the mod manager view + the BethINI editor.
    m_centralTabs = new QTabWidget(this);
    m_centralTabs->addTab(outer, "Mods");
    m_bethiniWindow = new solero::BethiniWindow(this);
    m_centralTabs->addTab(m_bethiniWindow, "BethINI");
    setCentralWidget(m_centralTabs);
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
        engine.undeploy(solero::AppConfig::instance().gameDir());
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
        auto result = engine.deploy(*profile, m_deployMode);
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
}

void MainWindow::refreshProfileCombo() {
    QSignalBlocker blocker(m_profileCombo);
    m_profileCombo->clear();
    m_profileCombo->addItems(m_profileMgr->profileNames());
}

void MainWindow::onDeployToggle() {
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
        statusBar()->showMessage("Deploying...");
        qApp->processEvents();

        solero::ProgressModal prog(this, "Deploy", "Deploying mods + sorting plugins with LOOT...");
        prog.show(); prog.pump();

        solero::DeployEngine engine(
            solero::AppConfig::instance().gameDir(),
            solero::AppConfig::instance().stagingDir());
        engine.setUserlistPath(profile->lootUserlistPath());
        auto result = engine.deploy(*profile, m_deployMode);

        prog.close();

        if (!result.success) {
            QMessageBox::critical(this, "Deploy Failed", result.errorMessage);
            return;
        }
        m_deployed = true;
        m_deployDirty = false;
        statusBar()->showMessage(
            QString("Deployed %1 files. %2 conflicts. Plugins sorted by LOOT.")
                .arg(result.filesDeployed)
                .arg(result.conflicts.conflictedPaths().size()));
        m_rightPane->setConflictIndex(result.conflicts);
        m_rightPane->setProfile(profile); // refresh plugin list - LOOT may have reordered it
        emit conflictsUpdated(result.conflicts);
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
        engine.undeploy(solero::AppConfig::instance().gameDir());

        prog.close();
        m_deployed = false;
        m_deployDirty = false;
        statusBar()->showMessage("Undeployed.");
    }

    updateDeployButton();
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

void MainWindow::onModsChanged() {
    auto* profile = m_profileMgr->activeProfile();
    if (profile) m_rightPane->refreshPlugins(profile);  // plugins follow enabled mods
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
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

    auto prep = solero::ModInstaller::prepare(archive);
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
            result = solero::ModInstaller::stageSimple(prep, staging);
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
            if (!imgDirs.isEmpty()) solero::ModInstaller::extractSubpaths(prep, imgDirs);
            extractProg->close();
            solero::FomodWizard wizard(&engine, prep.extractDir, this);
            if (wizard.exec() != QDialog::Accepted) { statusBar()->showMessage("Install cancelled."); return; }
            solero::ProgressModal stageProg(this, "Install", "Installing files...");
            stageProg.show(); stageProg.pump();
            result = solero::ModInstaller::stageFomod(prep, staging, wizard.result());
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
        result = solero::ModInstaller::stageSimple(prep, staging);
        stageProg.close();
    }

    if (!result.success) { QMessageBox::critical(this, "Install Failed", result.errorMessage); return; }

    if (!choiceLog.isEmpty()) {
        QJsonObject root;
        root["installer_version"] = "1.0";
        root["installed_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        root["steps"] = choiceLog;
        QString cp = staging + "/" + result.modId + "/fomod-choices.json";
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

    auto prep = solero::ModInstaller::prepare(archive);
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
            result = solero::ModInstaller::stageSimple(prep, staging, modId);
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
            if (!imgDirs.isEmpty()) solero::ModInstaller::extractSubpaths(prep, imgDirs);
            extractProg->close();
            solero::FomodWizard wizard(&engine, prep.extractDir, this);
            if (wizard.exec() != QDialog::Accepted) { statusBar()->showMessage("Reinstall cancelled."); return; }
            solero::ProgressModal stageProg(this, "Reinstall", "Installing files...");
            stageProg.show(); stageProg.pump();
            result = solero::ModInstaller::stageFomod(prep, staging, wizard.result(), modId);
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
        result = solero::ModInstaller::stageSimple(prep, staging, modId);
        stageProg.close();
    }

    if (!result.success) { QMessageBox::critical(this, "Reinstall Failed", result.errorMessage); return; }

    if (!choiceLog.isEmpty()) {
        QJsonObject root;
        root["installer_version"] = "1.0";
        root["installed_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        root["steps"] = choiceLog;
        QFile f(staging + "/" + modId + "/fomod-choices.json");
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
    existing->hasFomodChoices = !choiceLog.isEmpty();
    existing->sourceArchive = archive;
    profile->save();
    m_modListView->setProfile(profile);
    if (auto* p = m_profileMgr->activeProfile()) m_rightPane->refreshPlugins(p);
    if (m_deployed) { m_deployDirty = true; updateDeployButton(); }
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

void MainWindow::rebuildToolsMenu() {
    auto* menu = new QMenu(m_toolsBtn);
    for (const auto& t : m_toolStore->tools()) {
        QString id = t.id;
        menu->addAction(t.name, this, [this, id]{
            const auto& tools = m_toolStore->tools();
            for (const auto& e : tools) if (e.id == id) {
                statusBar()->showMessage("Running " + e.name + "...");
                qApp->processEvents();
                auto res = solero::ToolRunner::run(e,
                    solero::AppConfig::instance().gameDir(),
                    solero::AppConfig::instance().stagingDir());
                if (!res.launched) QMessageBox::warning(this, "Tool", res.error);
                else statusBar()->showMessage(e.name + " finished.");
                if (auto* p = m_profileMgr->activeProfile()) m_modListView->setProfile(p);
                break;
            }
        });
    }
    menu->addSeparator();
    menu->addAction("Add Tool...", this, &MainWindow::onAddTool);
    m_toolsBtn->setMenu(menu);
}

void MainWindow::onAddTool() {
    solero::ExecutableDialog dlg({}, this);
    if (dlg.exec() != QDialog::Accepted) return;
    solero::Executable e = dlg.result();
    if (e.id.isEmpty()) e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_toolStore->add(e);
    m_toolStore->save();
    rebuildToolsMenu();
}
