#include "ToolsManagerDialog.h"
#include "core/Types.h"
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
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

    auto* btnRow = new QHBoxLayout;
    auto* addBtn = new QPushButton("Add Tool\xe2\x80\xa6", this);
    auto* editBtn = new QPushButton("Edit\xe2\x80\xa6", this);
    auto* removeBtn = new QPushButton("Remove", this);
    auto* closeBtn = new QPushButton("Close", this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(editBtn);
    btnRow->addWidget(removeBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    outer->addLayout(btnRow);

    connect(addBtn, &QPushButton::clicked, this, [this]{ emit addToolRequested(); });
    connect(editBtn, &QPushButton::clicked, this, [this]{
        if (auto* item = m_list->currentItem())
            emit editToolRequested(item->data(Qt::UserRole).toString());
    });
    connect(removeBtn, &QPushButton::clicked, this, [this]{
        if (auto* item = m_list->currentItem())
            emit removeToolRequested(item->data(Qt::UserRole).toString());
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    refresh();
}

void ToolsManagerDialog::refresh() {
    m_list->clear();
    if (!m_tools) return;
    for (const auto& exe : *m_tools) {
        auto* item = new QListWidgetItem(QIcon(exe.iconPath), exe.name, m_list);
        item->setData(Qt::UserRole, exe.id);
    }
}

}
