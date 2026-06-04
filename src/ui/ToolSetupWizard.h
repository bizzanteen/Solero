#pragma once
#include <QDialog>
namespace solero { class ToolStore; }
namespace solero {
class ToolSetupWizard : public QDialog {
    Q_OBJECT
public:
    static void run(QWidget* parent, ToolStore* store);
private:
    ToolSetupWizard(QWidget* parent, ToolStore* store);
    ToolStore* m_store;
};
}
