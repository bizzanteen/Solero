#pragma once
#include <QDialog>

namespace solero {

class SetupPanel;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    SetupPanel* m_setupPanel = nullptr;
};

} // namespace solero
