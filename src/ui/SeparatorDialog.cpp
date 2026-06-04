#include "SeparatorDialog.h"
#include "ui/IconUtil.h"
#include "core/AppConfig.h"
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QColorDialog>
#include <QListWidget>
#include <QDir>

namespace solero {

SeparatorDialog::SeparatorDialog(const ModEntry& sep, QWidget* parent)
    : QDialog(parent), m_result(sep) {
    setWindowTitle("Edit Separator");
    auto* layout = new QFormLayout(this);

    m_nameEdit = new QLineEdit(sep.name, this);

    m_iconList = new QListWidget(this);
    m_iconList->setViewMode(QListView::IconMode);
    m_iconList->setIconSize(QSize(32, 32));
    m_iconList->setGridSize(QSize(92, 72));
    m_iconList->setUniformItemSizes(true);
    m_iconList->setMovement(QListView::Static);
    m_iconList->setResizeMode(QListView::Adjust);
    m_iconList->setWordWrap(true);
    m_iconList->setSpacing(2);
    m_iconList->setStyleSheet("background:#2b2b2b;");
    m_iconList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_iconList->setMaximumHeight(150);

    auto* none = new QListWidgetItem("None", m_iconList);
    none->setData(Qt::UserRole, QString());
    if (sep.icon.isEmpty()) m_iconList->setCurrentItem(none);

    const auto files = QDir(":/icons/separators").entryList(QStringList() << "*.svg", QDir::Files);
    for (const auto& f : files) {
        QString res = ":/icons/separators/" + f;
        QString base = f;
        if (base.endsWith(".svg")) base.chop(4);
        auto* it = new QListWidgetItem(renderSvgIcon(res, Qt::white, 32), base, m_iconList);
        it->setData(Qt::UserRole, res);
        if (res == sep.icon) m_iconList->setCurrentItem(it);
    }

    m_colorBtn = new QPushButton(sep.color.isEmpty() ? "(none)" : sep.color, this);
    if (!sep.color.isEmpty())
        m_colorBtn->setStyleSheet(QString("background-color: %1").arg(sep.color));

    layout->addRow("Name:", m_nameEdit);
    layout->addRow("Icon:", m_iconList);
    layout->addRow("Colour:", m_colorBtn);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(btns);

    connect(m_colorBtn, &QPushButton::clicked, this, &SeparatorDialog::pickColor);
    connect(btns, &QDialogButtonBox::accepted, this, [this]{
        m_result.name = m_nameEdit->text();
        m_result.icon = m_iconList->currentItem()
                        ? m_iconList->currentItem()->data(Qt::UserRole).toString()
                        : QString();
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
        AppConfig::instance().setLastSeparatorColor(c.name());
        AppConfig::instance().save();
    }
}

} // namespace solero
