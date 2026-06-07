#pragma once
#include <QDialog>
#include <QList>
#include "core/HealthCheck.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

namespace solero {

// Non-modal panel listing every aggregated HealthIssue grouped by severity.
// Double-clicking a row (or "Go to") asks the MainWindow to select the target
// mod/plugin; "Re-scan" asks it to recompute the issues.
class ProblemsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProblemsDialog(QWidget* parent = nullptr);
    void setIssues(const QList<HealthIssue>& issues);

signals:
    void rescanRequested();
    void goToMod(const QString& modId);
    void goToPlugin(const QString& pluginFilename);

private:
    void activateItem(QTreeWidgetItem* item);
    void goToCurrent();

    QTreeWidget* m_tree = nullptr;
    QLabel*      m_summary = nullptr;
    QPushButton* m_goToBtn = nullptr;
};

} // namespace solero
