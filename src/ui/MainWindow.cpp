#include "MainWindow.h"
#include "ModListView.h"
#include "RightPane.h"
#include "BottomPanel.h"
#include "SetupWizard.h"
#include "app/Application.h"
#include "core/AppConfig.h"
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

    if (m_profileMgr->profileNames().isEmpty())
        m_profileMgr->createProfile("Default");

    m_ipcServer = new solero::IPCServer(this);
    m_ipcServer->setTransactionLog(m_txLog);
    m_ipcServer->start("solero-ipc");

    setupToolbar();
    setupCentralWidget();
    statusBar()->showMessage("Ready");

    switchProfile(m_profileMgr->profileNames().first());
}

MainWindow::~MainWindow() {
    delete m_profileMgr;
    delete m_txLog;
}

QString MainWindow::profilesRoot() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/profiles";
}

QString MainWindow::txLogPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ai-transactions.json";
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
    profileMenuBtn->setMenu(profileMenu);
    profileMenuBtn->setPopupMode(QToolButton::InstantPopup);
    tb->addWidget(profileMenuBtn);
    tb->addSeparator();

    // Deploy toggle
    m_deployAction = tb->addAction("\xe2\x9c\x97 Not Deployed", this, &MainWindow::onDeployToggle);
    m_deployAction->setToolTip("Click to deploy mods to game directory");
    tb->addSeparator();

    // AI changes badge
    m_aiChangesLabel = new QLabel("AI: 0 changes", tb);
    tb->addWidget(m_aiChangesLabel);
    tb->addSeparator();

    // Game settings
    tb->addAction("Game Settings...", this, [this]{
        solero::SetupWizard wizard(this);
        if (wizard.exec() == QDialog::Accepted)
            statusBar()->showMessage("Game settings updated.");
    });
}

void MainWindow::setupCentralWidget() {
    auto* outer = new QSplitter(Qt::Vertical, this);

    m_splitter = new QSplitter(Qt::Horizontal, outer);
    m_modListView    = new solero::ModListView(m_splitter);
    m_rightPane = new solero::RightPane(m_splitter);
    m_splitter->addWidget(m_modListView);
    m_splitter->addWidget(m_rightPane);
    connect(m_modListView, &solero::ModListView::modSelected,
            m_rightPane,   &solero::RightPane::onModSelected);
    m_splitter->setSizes({640, 640});

    m_bottomPanel = new solero::BottomPanel(outer);
    outer->addWidget(m_splitter);
    outer->addWidget(m_bottomPanel);
    outer->setSizes({580, 200});

    setCentralWidget(outer);
}

void MainWindow::switchProfile(const QString& name) {
    if (name.isEmpty()) return;
    auto* profile = m_profileMgr->loadProfile(name);
    m_ipcServer->setActiveProfile(profile);
    m_modListView->setProfile(profile);
    m_rightPane->setProfile(profile);
    // Self-review fix: load previously-computed ConflictIndex if it exists
    QString conflictPath = solero::DeployEngine::conflictIndexPath(profile->path());
    if (QFile::exists(conflictPath))
        m_rightPane->setConflictIndex(solero::ConflictIndex::loadFromFile(conflictPath));
    setWindowTitle(QString("Solero - %1").arg(name));
    statusBar()->showMessage(QString("Loaded profile: %1").arg(name));
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

    if (!m_deployed) {
        statusBar()->showMessage("Deploying...");
        qApp->processEvents();

        solero::DeployEngine engine(
            solero::AppConfig::instance().gameDir(),
            solero::AppConfig::instance().stagingDir());
        auto result = engine.deploy(*profile, m_deployMode);

        if (!result.success) {
            QMessageBox::critical(this, "Deploy Failed", result.errorMessage);
            return;
        }
        m_deployed = true;
        statusBar()->showMessage(
            QString("Deployed %1 files. %2 conflicts.")
                .arg(result.filesDeployed)
                .arg(result.conflicts.conflictedPaths().size()));
        m_rightPane->setConflictIndex(result.conflicts);
        emit conflictsUpdated(result.conflicts);
    } else {
        auto ret = QMessageBox::question(this, "UnDeploy",
            "Remove all deployed mod links? Staged mods will not be affected.",
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) return;

        solero::DeployEngine engine(
            solero::AppConfig::instance().gameDir(),
            solero::AppConfig::instance().stagingDir());
        engine.undeploy(solero::AppConfig::instance().gameDir());
        m_deployed = false;
        statusBar()->showMessage("Undeployed.");
    }

    if (m_deployed) {
        m_deployAction->setText("\xe2\x9c\x93 Deployed");
        m_deployAction->setToolTip("Mods are deployed - click to undeploy");
    } else {
        m_deployAction->setText("\xe2\x9c\x97 Not Deployed");
        m_deployAction->setToolTip("Click to deploy mods to game directory");
    }
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
