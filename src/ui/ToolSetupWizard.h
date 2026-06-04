#pragma once
#include <QDialog>
#include <QPair>
#include <QList>
#include <QString>
namespace solero { class ToolStore; }
namespace solero {
class ToolSetupWizard : public QDialog {
    Q_OBJECT
public:
    static void run(QWidget* parent, ToolStore* store);
    ToolSetupWizard(QWidget* parent, ToolStore* store);
    void setModChoices(const QList<QPair<QString,QString>>& choices);
signals:
    void installModRequested(const QString& archivePath);
private:
    ToolStore* m_store;
    QList<QPair<QString,QString>> m_modChoices;
};
}
