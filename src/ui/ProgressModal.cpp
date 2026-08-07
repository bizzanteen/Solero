#include "ProgressModal.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QApplication>
#include <QFontMetrics>
#include <QMargins>
namespace solero {
ProgressModal::ProgressModal(QWidget* parent, const QString& title, const QString& message)
    : QDialog(parent) {
    setWindowTitle(title);
    setModal(true);
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    auto* v = new QVBoxLayout(this);
    m_label = new QLabel(message, this);
    m_label->setWordWrap(true); // fallback for anything past the cap
    fitToMessage(message);
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 0); // indeterminate
    m_bar->setTextVisible(false);
    v->addWidget(m_label);
    v->addWidget(m_bar);
}
// Widen so the message sits on one line. Grow-only, so a later shorter message
// doesn't make the dialog jump about mid-operation.
void ProgressModal::fitToMessage(const QString& message) {
    const int text = m_label->fontMetrics().horizontalAdvance(message);
    const QMargins m = layout() ? layout()->contentsMargins() : QMargins(11, 11, 11, 11);
    const int want = text + m.left() + m.right() + 8;
    setMinimumWidth(qBound(380, want, 900));
}

void ProgressModal::setMessage(const QString& message) {
    m_label->setText(message);
    fitToMessage(message);
    pump();
}
void ProgressModal::setProgress(int done, int total) {
    if (total <= 0) { m_bar->setRange(0, 0); }
    else { m_bar->setRange(0, total); m_bar->setValue(done); }
    pump();
}
void ProgressModal::enableCancel() {
    if (m_cancelBtn) return;
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    layout()->addWidget(m_cancelBtn);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]{
        m_cancelled = true;
        m_cancelBtn->setEnabled(false);
        m_cancelBtn->setText(tr("Cancelling") + QChar(0x2026));
    });
}
void ProgressModal::pump() { QApplication::processEvents(); }
}
