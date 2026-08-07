#pragma once
// Bug/crash report dialog. Two modes:
//   Issue - Help > Report Issue (summary / what happened / expected / steps)
//   Crash - shown on the launch after a caught crash, plus a detailed-logging
//           checkbox for the next launch only.
// Log tail and system info attach automatically, with home paths redacted.
#include <QDialog>

#include "report/ReportSubmitter.h"

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QCheckBox;
class QPushButton;

namespace solero {

class ReportDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Issue, Crash };

    explicit ReportDialog(Mode mode, QWidget* parent = nullptr);

    // Context for the system-info sections. modCount<0 = unknown. `logOffset` (a crash
    // marker's recorded byte offset) trims the attached log to the crash section; 0
    // attaches the normal last-~15 KB tail.
    void setContext(int modCount, qint64 logOffset = 0);

private slots:
    void onSend();
    void onSucceeded(const QString& issueUrl);
    void onFailed(const QString& error);

private:
    void collectFieldsAndSubmit();
    void setBusy(bool busy);
    QMap<QString, QString> fields() const;

    Mode m_mode;
    int  m_modCount = -1;
    qint64 m_logOffset = 0;

    ReportSubmitter* m_submitter = nullptr;

    // Issue-mode inputs.
    QLineEdit*      m_summary = nullptr;
    QPlainTextEdit* m_whatHappened = nullptr;
    QPlainTextEdit* m_expected = nullptr;
    QPlainTextEdit* m_steps = nullptr;
    // Crash-mode inputs.
    QPlainTextEdit* m_whatDoing = nullptr;
    QCheckBox*      m_verboseNext = nullptr;

    QLabel*           m_status = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QPushButton*      m_sendBtn = nullptr;
    QPushButton*      m_browserBtn = nullptr; // "Open in browser instead" (fallback)
};

} // namespace solero
