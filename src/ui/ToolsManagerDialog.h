#pragma once
#include <QDialog>
#include <QList>
class QListWidget;
namespace solero {
struct Executable;
// Manage-tools list view. It operates on the active profile's executables: the
// caller passes a pointer to that list (Profile::executables()); the dialog only
// reads it for display and emits add/edit/remove signals the caller handles
// (mutating + persisting the profile, then calling refresh()).
class ToolsManagerDialog : public QDialog {
    Q_OBJECT
public:
    ToolsManagerDialog(const QList<Executable>* tools, QWidget* parent = nullptr);
    void refresh();
signals:
    void editToolRequested(const QString& id);
    void removeToolRequested(const QString& id);
    void addToolRequested();
private:
    const QList<Executable>* m_tools;
    QListWidget* m_list;
};
}
