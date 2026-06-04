#include "DownloadsTab.h"
#include "core/AppConfig.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QDir>
namespace solero {
DownloadsTab::DownloadsTab(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->addWidget(new QLabel("Archives in your downloads folder. Select one and Install.", this));
    m_list = new QListWidget(this);
    v->addWidget(m_list, 1);
    auto* bar = new QHBoxLayout;
    auto* refreshBtn = new QPushButton("Refresh", this);
    auto* installBtn = new QPushButton("Install Selected", this);
    bar->addWidget(refreshBtn); bar->addStretch(); bar->addWidget(installBtn);
    v->addLayout(bar);
    connect(refreshBtn, &QPushButton::clicked, this, &DownloadsTab::refresh);
    connect(installBtn, &QPushButton::clicked, this, [this]{
        auto* it = m_list->currentItem();
        if (it) emit installRequested(it->data(Qt::UserRole).toString());
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it){
        if (it) emit installRequested(it->data(Qt::UserRole).toString());
    });
    refresh();
}
void DownloadsTab::refresh() {
    m_list->clear();
    QString dir = AppConfig::instance().downloadsDir();
    if (dir.isEmpty()) return;
    QDir d(dir);
    const auto files = d.entryList({"*.zip","*.7z","*.rar","*.tar","*.gz"}, QDir::Files, QDir::Time);
    for (const QString& f : files) {
        auto* it = new QListWidgetItem(f, m_list);
        it->setData(Qt::UserRole, d.absoluteFilePath(f));
    }
}
}
