#include "SeparatorDialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QColorDialog>

namespace solero {

SeparatorDialog::SeparatorDialog(const ModEntry& sep, QWidget* parent)
    : QDialog(parent), m_result(sep) {
    setWindowTitle("Edit Separator");
    auto* layout = new QFormLayout(this);

    m_nameEdit = new QLineEdit(sep.name, this);
    m_iconEdit = new QLineEdit(sep.icon, this);
    m_colorBtn = new QPushButton(sep.color.isEmpty() ? "(none)" : sep.color, this);
    if (!sep.color.isEmpty())
        m_colorBtn->setStyleSheet(QString("background-color: %1").arg(sep.color));

    layout->addRow("Name:", m_nameEdit);
    layout->addRow("Icon (emoji):", m_iconEdit);
    layout->addRow("Colour:", m_colorBtn);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(btns);

    connect(m_colorBtn, &QPushButton::clicked, this, &SeparatorDialog::pickColor);
    connect(btns, &QDialogButtonBox::accepted, this, [this]{
        m_result.name = m_nameEdit->text();
        m_result.icon = m_iconEdit->text();
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SeparatorDialog::pickColor() {
    QColor c = QColorDialog::getColor(
        m_result.color.isEmpty() ? Qt::gray : QColor(m_result.color), this);
    if (c.isValid()) {
        m_result.color = c.name();
        m_colorBtn->setText(c.name());
        m_colorBtn->setStyleSheet(QString("background-color: %1").arg(c.name()));
    }
}

} // namespace solero
