#pragma once
#include <QWidget>

class QLineEdit;
class QLabel;
class QPushButton;
class QComboBox;
class QGroupBox;

namespace solero {

class SetupPanel : public QWidget {
    Q_OBJECT
public:
    explicit SetupPanel(QWidget* parent = nullptr);

    bool isValid() const;
    void save();

signals:
    void validityChanged(bool valid);

private:
    void browseGameDir();
    void browseStagingDir();
    void browseDownloadsDir();
    void refreshValidity();
    void refreshGameChoices();

    QComboBox*  m_gameDetectedCombo = nullptr;
    QComboBox*  m_gameTypeCombo = nullptr;
    QLabel*     m_detectedLabel = nullptr;
    QLabel*     m_gameHint = nullptr;
    QGroupBox*  m_gameGroup = nullptr;
    QLineEdit*  m_gameDirEdit = nullptr;
    QLineEdit*  m_stagingDirEdit = nullptr;
    QLineEdit*  m_downloadsEdit = nullptr;
    QLabel*     m_statusLabel = nullptr;
};

} // namespace solero
