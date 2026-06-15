#pragma once
#include <QDialog>

class QPushButton;
class QLineEdit;

namespace solero {

class SetupPanel;

class SetupWizard : public QDialog {
    Q_OBJECT
public:
    explicit SetupWizard(QWidget* parent = nullptr);

private:
    SetupPanel*  m_panel = nullptr;
    QPushButton* m_acceptBtn = nullptr;
    QLineEdit*   m_apiKeyEdit = nullptr;
};

} // namespace solero
