#include "SetupWizard.h"
#include "SetupPanel.h"
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>

namespace solero {

SetupWizard::SetupWizard(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Welcome to Solero - Setup");
    setMinimumWidth(560);
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    m_panel = new SetupPanel(this);
    layout->addWidget(m_panel);

    auto* btns = new QDialogButtonBox(this);
    m_acceptBtn = btns->addButton("Set Up Solero", QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(btns);

    m_acceptBtn->setEnabled(m_panel->isValid());
    connect(m_panel, &SetupPanel::validityChanged, m_acceptBtn, &QPushButton::setEnabled);
    connect(btns, &QDialogButtonBox::accepted, this, [this]{
        m_panel->save();
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace solero
