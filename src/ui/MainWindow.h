#pragma once
#include <QMainWindow>
#include <QAction>
#include "core/ProfileManager.h"
#include "ai/AITransaction.h"
#include "ipc/IPCServer.h"
#include "deploy/DeployEngine.h"
#include "deploy/DeployMode.h"
#include "deploy/ConflictIndex.h"

class QSplitter;
class QComboBox;
class QToolButton;
class QLabel;
class QTabWidget;

namespace solero {
class ModListView;
class PluginListView;
class RightPane;
class BottomPanel;
class BethiniWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

signals:
    void conflictsUpdated(const solero::ConflictIndex& index);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupToolbar();
    void setupCentralWidget();
    void switchProfile(const QString& name);
    void refreshProfileCombo();
    void onDeployToggle();
    void refreshDeployState();   // detect an existing deployment on startup
    void updateDeployButton();   // sync the toggle's text/tooltip to m_deployed
    void onOpenBethini();
    void onNewProfile();
    void onDeleteProfile();
    void onInstallMod();
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();

    solero::ProfileManager* m_profileMgr;
    solero::AITransactionLog* m_txLog;
    solero::IPCServer* m_ipcServer;

    bool m_deployed = false;
    solero::DeployMode m_deployMode = solero::DeployMode::HardLink;
    QAction* m_deployAction = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QSplitter* m_splitter = nullptr;
    solero::ModListView*    m_modListView = nullptr;
    solero::RightPane*      m_rightPane = nullptr;
    solero::BottomPanel*    m_bottomPanel = nullptr;
    solero::BethiniWindow*  m_bethiniWindow = nullptr;
    QTabWidget*             m_centralTabs = nullptr;
    QLabel* m_aiChangesLabel = nullptr;

    QString profilesRoot() const;
    QString txLogPath() const;
};
