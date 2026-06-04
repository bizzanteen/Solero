#pragma once
#include <QWidget>

class QLineEdit;
class QLabel;
class QPushButton;
class QComboBox;

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

    QComboBox*  m_gameDetectedCombo = nullptr;
    QLineEdit*  m_gameDirEdit = nullptr;
    QLineEdit*  m_stagingDirEdit = nullptr;
    QLineEdit*  m_downloadsEdit = nullptr;
    QLabel*     m_statusLabel = nullptr;
};

} // namespace solero
