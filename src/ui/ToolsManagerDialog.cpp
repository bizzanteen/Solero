#include "ToolsManagerDialog.h"
#include <algorithm>
#include "core/Types.h"
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QIcon>

namespace solero {

ToolsManagerDialog::ToolsManagerDialog(const QList<Executable>* tools, QWidget* parent)
    : QDialog(parent), m_tools(tools) {
    setWindowTitle("Manage Tools");
    setMinimumSize(420, 320);

    auto* outer = new QVBoxLayout(this);
    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(24, 24));
    outer->addWidget(m_list);

    auto* btnRow = new QDialogButtonBox(this);
    auto* addBtn = new QPushButton("Add Tool\xe2\x80\xa6", this);
    auto* editBtn = new QPushButton("Edit\xe2\x80\xa6", this);
    auto* removeBtn = new QPushButton("Remove", this);
    btnRow->addButton(addBtn, QDialogButtonBox::ActionRole);
    btnRow->addButton(editBtn, QDialogButtonBox::ActionRole);
    btnRow->addButton(removeBtn, QDialogButtonBox::ActionRole);
    btnRow->addButton(QDialogButtonBox::Close);
    outer->addWidget(btnRow);

    connect(addBtn, &QPushButton::clicked, this, [this]{ emit addToolRequested(); });
    connect(editBtn, &QPushButton::clicked, this, [this]{
        if (auto* item = m_list->currentItem())
            emit editToolRequested(item->data(Qt::UserRole).toString());
    });
    connect(removeBtn, &QPushButton::clicked, this, [this]{
        if (auto* item = m_list->currentItem())
            emit removeToolRequested(item->data(Qt::UserRole).toString());
    });
    connect(btnRow, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refresh();
}

void ToolsManagerDialog::refresh() {
    m_list->clear();
    if (!m_tools) return;
    // Show tools name-sorted (case-insensitive); the store order is unchanged.
    auto sorted = *m_tools;
    std::sort(sorted.begin(), sorted.end(), [](const Executable& a, const Executable& b){
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    for (const auto& exe : sorted) {
        auto* item = new QListWidgetItem(QIcon(exe.iconPath), exe.name, m_list);
        item->setData(Qt::UserRole, exe.id);
    }
}

}
