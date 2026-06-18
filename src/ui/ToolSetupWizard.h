#pragma once
#include <QDialog>
#include <QPair>
#include <QList>
#include <QSet>
#include <QString>
#include "core/Types.h"
namespace solero { class ToolStore; }
namespace solero {
class ToolSetupWizard : public QDialog {
    Q_OBJECT
public:
    static void run(QWidget* parent, ToolStore* store);
    // installedKeys = the active profile's installed tool keys (each entry's id and
    // lower-cased name). The "(installed)" badge is computed PER profile from this
    // set, not from the global ToolStore. Empty set = nothing installed (e.g. a
    // brand-new profile), so every preset is offered.
    ToolSetupWizard(QWidget* parent, ToolStore* store,
                    const QSet<QString>& installedKeys = {});
    void setModChoices(const QList<QPair<QString,QString>>& choices);
    // The executables actually set up during this wizard session (not the whole
    // global store). MainWindow::onAddTool2 adds only these to the active profile.
    const QList<Executable>& setUpTools() const { return m_setUp; }
signals:
    void installModRequested(const QString& archivePath);
private:
    // id OR lower-cased name of each tool installed in the active profile.
    QSet<QString> m_installedKeys;
    ToolStore* m_store;
    QList<QPair<QString,QString>> m_modChoices;
    // Tools set up in this wizard session (see setUpTools()).
    QList<Executable> m_setUp;
};
}
