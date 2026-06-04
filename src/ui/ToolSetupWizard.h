#pragma once
#include <QDialog>
namespace solero { class ToolStore; }
namespace solero {
class ToolSetupWizard : public QDialog {
    Q_OBJECT
public:
    static void run(QWidget* parent, ToolStore* store);
    ToolSetupWizard(QWidget* parent, ToolStore* store);
signals:
    void installModRequested(const QString& archivePath);
private:
    ToolStore* m_store;
};
}
