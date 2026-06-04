#pragma once
#include <QDialog>
class QListWidget;
namespace solero { class ToolStore; }
namespace solero {
class ToolsManagerDialog : public QDialog {
    Q_OBJECT
public:
    ToolsManagerDialog(ToolStore* store, QWidget* parent = nullptr);
    void refresh();
signals:
    void editToolRequested(const QString& id);
    void removeToolRequested(const QString& id);
    void addToolRequested();
private:
    ToolStore* m_store;
    QListWidget* m_list;
};
}
