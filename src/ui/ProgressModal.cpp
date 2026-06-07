#include "ProgressModal.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QApplication>
namespace solero {
ProgressModal::ProgressModal(QWidget* parent, const QString& title, const QString& message)
    : QDialog(parent) {
    setWindowTitle(title);
    setModal(true);
    setMinimumWidth(380);
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    auto* v = new QVBoxLayout(this);
    m_label = new QLabel(message, this);
    m_label->setWordWrap(true);
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 0); // indeterminate
    m_bar->setTextVisible(false);
    v->addWidget(m_label);
    v->addWidget(m_bar);
}
void ProgressModal::setMessage(const QString& message) { m_label->setText(message); pump(); }
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
        m_cancelBtn->setText(tr("Cancelling\xe2\x80\xa6"));
    });
}
void ProgressModal::pump() { QApplication::processEvents(); }
}
