#include "ui/ReportDialog.h"

#include "core/AppConfig.h"
#include "core/Log.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace solero {

ReportDialog::ReportDialog(Mode mode, QWidget* parent)
    : QDialog(parent), m_mode(mode) {
    setWindowTitle(mode == Mode::Crash ? tr("Report a Crash") : tr("Report an Issue"));
    setModal(true);
    resize(520, 460);

    m_submitter = new ReportSubmitter(this);
    connect(m_submitter, &ReportSubmitter::succeeded, this, &ReportDialog::onSucceeded);
    connect(m_submitter, &ReportSubmitter::failed,    this, &ReportDialog::onFailed);

    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(
        mode == Mode::Crash
            ? tr("Solero recovered from a crash on the last run. Telling us what you "
                 "were doing helps us reproduce and fix it.")
            : tr("Found a bug? Describe it below and it will be filed on the project "
                 "tracker. No GitHub account needed."),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* form = new QFormLayout();
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    root->addLayout(form);

    if (mode == Mode::Crash) {
        m_whatDoing = new QPlainTextEdit(this);
        m_whatDoing->setPlaceholderText(
            tr("e.g. I clicked Deploy after enabling three texture mods…"));
        form->addRow(tr("What were you doing when it crashed?"), m_whatDoing);

        m_verboseNext = new QCheckBox(
            tr("Enable detailed logging on next launch to help reproduce this"), this);
        root->addWidget(m_verboseNext);
    } else {
        m_summary = new QLineEdit(this);
        m_summary->setPlaceholderText(tr("One-line summary"));
        form->addRow(tr("Summary"), m_summary);

        m_whatHappened = new QPlainTextEdit(this);
        form->addRow(tr("What happened?"), m_whatHappened);

        m_expected = new QPlainTextEdit(this);
        form->addRow(tr("What did you expect?"), m_expected);

        m_steps = new QPlainTextEdit(this);
        m_steps->setPlaceholderText(tr("Optional"));
        form->addRow(tr("Steps to reproduce"), m_steps);
    }

    auto* note = new QLabel(
        tr("Your log and system info will be attached (home paths redacted)."), this);
    note->setWordWrap(true);
    QFont nf = note->font();
    nf.setItalic(true);
    note->setFont(nf);
    root->addWidget(note);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_status->setOpenExternalLinks(true);
    m_status->hide();
    root->addWidget(m_status);

    m_buttons = new QDialogButtonBox(this);
    m_sendBtn = m_buttons->addButton(tr("Send Report"), QDialogButtonBox::AcceptRole);
    m_browserBtn = m_buttons->addButton(tr("Open in browser instead"),
                                        QDialogButtonBox::ActionRole);
    m_buttons->addButton(QDialogButtonBox::Cancel);
    root->addWidget(m_buttons);

    connect(m_sendBtn, &QPushButton::clicked, this, &ReportDialog::onSend);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_browserBtn, &QPushButton::clicked, this, [this]() {
        const QJsonObject payload = ReportSubmitter::buildPayload(
            m_mode == Mode::Crash ? ReportSubmitter::Kind::Crash
                                  : ReportSubmitter::Kind::Issue,
            fields(),
            ReportSubmitter::gatherLogTail(m_logOffset),
            ReportSubmitter::gatherSystemInfo(m_modCount));
        QDesktopServices::openUrl(QUrl(ReportSubmitter::prefilledIssueUrl(payload)));
    });

    // If the relay hasn't been configured, direct submit is impossible - disable Send
    // (with a pointer to the setup) and lead the user to the browser fallback.
    if (!ReportSubmitter::relayConfigured()) {
        m_sendBtn->setEnabled(false);
        m_sendBtn->setToolTip(
            tr("Report relay not configured. See tools/report-relay/README.md, then set "
               "kReportRelayUrl (or the SOLERO_REPORT_RELAY env var)."));
        m_status->setText(
            tr("Direct submission isn't configured yet - use \"Open in browser "
               "instead\" to file this on GitHub."));
        m_status->show();
    } else {
        m_browserBtn->hide(); // only surfaced as a fallback after a failure
    }
}

void ReportDialog::setContext(int modCount, qint64 logOffset) {
    m_modCount = modCount;
    m_logOffset = logOffset;
}

QMap<QString, QString> ReportDialog::fields() const {
    QMap<QString, QString> f;
    if (m_mode == Mode::Crash) {
        if (m_whatDoing) f["whatDoing"] = m_whatDoing->toPlainText();
    } else {
        if (m_summary)      f["summary"]      = m_summary->text();
        if (m_whatHappened) f["whatHappened"] = m_whatHappened->toPlainText();
        if (m_expected)     f["expected"]     = m_expected->toPlainText();
        if (m_steps)        f["steps"]        = m_steps->toPlainText();
    }
    return f;
}

void ReportDialog::onSend() {
    // Validate: need a non-empty description.
    const QString desc = m_mode == Mode::Crash
        ? (m_whatDoing ? m_whatDoing->toPlainText().trimmed() : QString())
        : (m_whatHappened ? m_whatHappened->toPlainText().trimmed() : QString());
    if (desc.isEmpty()) {
        m_status->setText(m_mode == Mode::Crash
            ? tr("Please describe what you were doing before sending.")
            : tr("Please describe what happened before sending."));
        m_status->show();
        return;
    }

    // Crash-mode one-shot: enable verbose logging for the next launch only.
    if (m_mode == Mode::Crash && m_verboseNext && m_verboseNext->isChecked()) {
        AppConfig::instance().setVerboseNextLaunch(true);
        AppConfig::instance().save();
    }

    collectFieldsAndSubmit();
}

void ReportDialog::collectFieldsAndSubmit() {
    setBusy(true);
    m_status->setText(tr("Sending report…"));
    m_status->show();

    const QJsonObject payload = ReportSubmitter::buildPayload(
        m_mode == Mode::Crash ? ReportSubmitter::Kind::Crash
                              : ReportSubmitter::Kind::Issue,
        fields(),
        ReportSubmitter::gatherLogTail(m_logOffset),
        ReportSubmitter::gatherSystemInfo(m_modCount));

    m_submitter->submit(payload);
}

void ReportDialog::setBusy(bool busy) {
    m_sendBtn->setEnabled(!busy && ReportSubmitter::relayConfigured());
    if (m_summary)      m_summary->setEnabled(!busy);
    if (m_whatHappened) m_whatHappened->setEnabled(!busy);
    if (m_expected)     m_expected->setEnabled(!busy);
    if (m_steps)        m_steps->setEnabled(!busy);
    if (m_whatDoing)    m_whatDoing->setEnabled(!busy);
    if (m_verboseNext)  m_verboseNext->setEnabled(!busy);
}

void ReportDialog::onSucceeded(const QString& issueUrl) {
    setBusy(false);
    m_status->setText(
        tr("Report sent. Thank you! Track it here: <a href=\"%1\">%1</a>").arg(issueUrl));
    m_status->show();
    // Swap Send for a Close affordance; the report is done.
    m_sendBtn->setEnabled(false);
    m_browserBtn->hide();
    if (auto* cancel = m_buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(tr("Close"));
}

void ReportDialog::onFailed(const QString& error) {
    setBusy(false);
    qCWarning(lcApp) << "report dialog: submit failed:" << error;
    m_status->setText(
        tr("Couldn't send the report: %1<br>You can file it manually instead.").arg(error));
    m_status->show();
    m_browserBtn->show(); // offer the browser fallback
}

} // namespace solero
