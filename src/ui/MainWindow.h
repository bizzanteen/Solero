#pragma once
#include <QMainWindow>
#include <QAction>
#include <QPair>
#include <QHash>
#include <QJsonObject>
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

class QSplitter;
class QComboBox;
class QToolButton;
class QLabel;
class QTabWidget;
class QMenu;
class QPushButton;
class QStackedWidget;

namespace solero {
class NexusWebView;
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

public slots:
    void handleNxmUrl(const QString& url);

signals:
    void conflictsUpdated(const solero::ConflictIndex& index);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupToolbar();
    void setupCentralWidget();
    void switchProfile(const QString& name);
    void refreshProfileCombo();
    void onDeployToggle();
    bool deployCurrent();        // perform a deploy of the active profile; returns success
    void showRunLock(const QString& toolName);
    void hideRunLock();
    void refreshDeployState();   // detect an existing deployment on startup
    void updateDeployButton();   // sync the toggle's text/tooltip to m_deployed
    void updatePluginNotice();   // show/hide the Plugins-tab staleness notice
    void updateSortButton();     // enable "Sort Now" only when deployed && load order dirty
    void onLoadOrderChanged();   // user manually reordered a plugin -> mark order dirty
    void onSortRequested();      // run LOOT on the current load order
    void onNewProfile();
    void onDeleteProfile();
    void onImportMo2();
    void onInstallMod();
    void onToggleNexus(bool on);
    void onNexusDownload(const QString& modId, const QString& fileId,
                         const QString& fileName, const QString& version);
    void installFromArchive(const QString& archive);
    void onReinstallMod(const QString& modId);
    void onEndorseMod(const QString& modId);
    void onCheckUpdates();
    void onIdentifyMod(const QString& modId);
    void onModsChanged();
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();
    void onRunTool(const solero::Executable& exe);
    void onAddTool2();
    QString ensureOutputMod(const QString& name);
    // Tools are global but their output mods live in whichever profile was active
    // at setup - so resolve/remove a mod across all profiles, not just the active one.
    QString modNameAnywhere(const QString& id) const;
    void removeModEverywhere(const QString& id);
    void onEditTool(const QString& id);
    void onRemoveTool(const QString& id);
    void onManageTools();
    QList<QPair<QString,QString>> modChoices() const;
    void rebuildToolsMenu();
    void onOpenBethini();
    void onPlay();

    solero::ProfileManager* m_profileMgr;
    solero::AITransactionLog* m_txLog;
    solero::IPCServer* m_ipcServer;

    bool m_deployed = false;
    bool m_deployDirty = false;
    bool m_loadOrderDirty = false; // user manually reordered plugins since last sort/deploy
    bool m_toolRunning = false;  // guards re-entrancy while a tool runs (nested event loop)
    bool m_warnedMissingAppData = false; // one-time warning when AppData can't be located
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
    solero::DownloadManager* m_downloads = nullptr;
    solero::NexusWebView* m_nexusWeb = nullptr;
    QStackedWidget* m_centralStack = nullptr;
    QWidget* m_modManagerPage = nullptr;
    QAction* m_browseAction = nullptr;
    // Pending Nexus metadata for in-flight nxm downloads, keyed by saved filename.
    // Written to a <archive>.solero-nexus.json sidecar when the download finishes.
    QHash<QString, QJsonObject> m_nxmMeta;

    QWidget* m_runOverlay = nullptr;
    QLabel* m_runLockLabel = nullptr;
    QPushButton* m_unlockBtn = nullptr;

    QString profilesRoot() const;
    QString txLogPath() const;
};
