#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace solero {

class SetupPanel;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // Show the SKSE version currently installed for the active profile. Empty
    // string -> displays "Not installed".
    void setSkseInstalledVersion(const QString& version);

signals:
    // Emitted when the user clicks "Connect to Nexus": MainWindow opens the
    // embedded Nexus browser at the personal API-key page. The dialog closes.
    void connectNexusRequested();

    // Emitted when the user chooses a specific SKSE build to install. MainWindow
    // performs the actual download+install via its Nexus plumbing.
    void skseInstallRequested(const QString& fileId, const QString& version);

private:
    SetupPanel* m_setupPanel = nullptr;
    QCheckBox* m_confirmDelete = nullptr;
    QCheckBox* m_cycleSeparatorColors = nullptr;
    QCheckBox* m_dataShowAllFiles = nullptr;
    QCheckBox* m_promptAfterBrowserDownload = nullptr;
    QCheckBox* m_autoCheckUpdates = nullptr;
    QComboBox* m_deployCombo = nullptr;
    QLabel* m_nxmStatus = nullptr;
    QLabel* m_nexusStatus = nullptr;
    QLineEdit* m_keyEdit = nullptr;
    QPushButton* m_signOutBtn = nullptr;
    QLineEdit* m_jackifyEdit = nullptr;
    QLabel* m_skseVersionLabel = nullptr;
    QPushButton* m_skseChangeBtn = nullptr;
    QComboBox* m_serverCombo = nullptr;
};

} // namespace solero
