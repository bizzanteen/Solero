#pragma once
#include <QDialog>
#include <QString>
#include <QList>
#include <QHash>
class QPushButton;
namespace solero {
// Lists a freshly-installed mod's missing Nexus requirements and lets the user
// install them. On-Nexus requirements get an Install button; off-site ones are
// listed greyed with no button. Install clicks are emitted via installRequested;
// the owner performs the actual download/install and placement.
class RequirementsDialog : public QDialog {
    Q_OBJECT
public:
    struct Item { QString modId, modName, notes; bool external = false; };
    RequirementsDialog(const QString& dependentName, const QList<Item>& missing,
                       QWidget* parent = nullptr);
signals:
    // Emitted once per requirement the user chooses to install (also fired for
    // each item when "Install all" is clicked). modId is the Nexus mod id.
    void installRequested(const QString& modId, const QString& modName);
private:
    void markInstalling(const QString& modId); // disable that row's button, show "Installing…"
    QHash<QString, QPushButton*> m_buttons;     // modId -> its Install button
    QPushButton* m_installAll = nullptr;
};
}
