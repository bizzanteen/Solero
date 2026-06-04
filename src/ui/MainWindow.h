#pragma once
#include <QMainWindow>
#include <QAction>
#include "core/ProfileManager.h"
#include "ai/AITransaction.h"
#include "ipc/IPCServer.h"
#include "deploy/DeployEngine.h"
#include "deploy/DeployMode.h"
#include "deploy/ConflictIndex.h"
#include "tools/ToolStore.h"
#include "tools/ToolRunner.h"
#include "ui/ExecutableDialog.h"

class QSplitter;
class QComboBox;
class QToolButton;
class QLabel;
class QTabWidget;
class QMenu;

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
    void onNewProfile();
    void onDeleteProfile();
    void onImportMo2();
    void onInstallMod();
    void installFromArchive(const QString& archive);
    void onReinstallMod(const QString& modId);
    void onModsChanged();
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();
    void onRunTool(const solero::Executable& exe);
    void onAddTool2();
    QString ensureOutputMod(const QString& name);
    void onEditTool(const QString& id);
    void onRemoveTool(const QString& id);
    void rebuildToolsMenu();
    void onOpenBethini();
    void onPlay();

    solero::ProfileManager* m_profileMgr;
    solero::AITransactionLog* m_txLog;
    solero::IPCServer* m_ipcServer;

    bool m_deployed = false;
    bool m_deployDirty = false;
    solero::DeployMode m_deployMode = solero::DeployMode::HardLink;
    QAction* m_deployAction = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QSplitter* m_splitter = nullptr;
    solero::ModListView*    m_modListView = nullptr;
    solero::RightPane*      m_rightPane = nullptr;
    solero::BottomPanel*    m_bottomPanel = nullptr;
    solero::BethiniWindow*  m_bethiniWindow = nullptr;
    QToolButton* m_toolsBtn = nullptr;
    QMenu* m_toolsMenu = nullptr;
    solero::ToolStore* m_toolStore = nullptr;

    QString profilesRoot() const;
    QString txLogPath() const;
};
