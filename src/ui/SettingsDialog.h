#pragma once
#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace solero {

class SetupPanel;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    SetupPanel* m_setupPanel = nullptr;
    QCheckBox* m_confirmDelete = nullptr;
    QCheckBox* m_cycleSeparatorColors = nullptr;
    QCheckBox* m_dataShowAllFiles = nullptr;
    QLabel* m_nxmStatus = nullptr;
    QLabel* m_nexusStatus = nullptr;
    QLineEdit* m_keyEdit = nullptr;
    QPushButton* m_signOutBtn = nullptr;
};

} // namespace solero
