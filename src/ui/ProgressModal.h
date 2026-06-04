#pragma once
#include <QDialog>
class QLabel;
class QProgressBar;
namespace solero {
// A simple modal shown during a blocking operation. Indeterminate by default;
// call setProgress(done,total) to switch to a determinate bar.
class ProgressModal : public QDialog {
    Q_OBJECT
public:
    explicit ProgressModal(QWidget* parent, const QString& title, const QString& message);
    void setMessage(const QString& message);
    void setProgress(int done, int total); // total<=0 => indeterminate
    void pump(); // process events so the UI repaints during synchronous work
private:
    QLabel* m_label;
    QProgressBar* m_bar;
};
}
