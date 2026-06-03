#pragma once
#include <QMainWindow>
#include <QAction>
#include "core/ProfileManager.h"
#include "ai/AITransaction.h"
#include "ipc/IPCServer.h"

class QSplitter;
class QComboBox;
class QToolButton;
class QLabel;

namespace solero {
class ModListView;
class PluginListView;
class BottomPanel;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupToolbar();
    void setupCentralWidget();
    void switchProfile(const QString& name);
    void refreshProfileCombo();
    void onDeployToggle();
    void onNewProfile();
    void onDeleteProfile();
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();

    solero::ProfileManager* m_profileMgr;
    solero::AITransactionLog* m_txLog;
    solero::IPCServer* m_ipcServer;

    bool m_deployed = false;
    QAction* m_deployAction = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QSplitter* m_splitter = nullptr;
    solero::ModListView*    m_modListView = nullptr;
    solero::PluginListView* m_pluginListView = nullptr;
    solero::BottomPanel*    m_bottomPanel = nullptr;
    QLabel* m_aiChangesLabel = nullptr;

    QString profilesRoot() const;
    QString txLogPath() const;
};
