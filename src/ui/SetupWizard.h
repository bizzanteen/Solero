#pragma once
#include <QDialog>

class QLineEdit;
class QLabel;
class QPushButton;
class QComboBox;

namespace solero {

class SetupWizard : public QDialog {
    Q_OBJECT
public:
    explicit SetupWizard(QWidget* parent = nullptr);

private:
    void autoDetect();
    void browseGameDir();
    void browseStagingDir();
    void onAccept();
    void updateAcceptState();

    QComboBox*  m_gameDetectedCombo;
    QLineEdit*  m_gameDirEdit;
    QLineEdit*  m_stagingDirEdit;
    QPushButton* m_acceptBtn;
    QLabel*     m_statusLabel;
};

} // namespace solero
