#include "DownloadsTab.h"
#include "core/AppConfig.h"
#include "core/Profile.h"
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMenu>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QDateTime>
#include <QSet>
#include <QColor>
#include <QItemSelectionModel>
#include <QModelIndex>

namespace solero {

namespace {

// Sorts by the numeric value stored in Qt::UserRole rather than display text.
class NumItem : public QTableWidgetItem {
public:
    explicit NumItem(const QString& text) : QTableWidgetItem(text) {}
    bool operator<(const QTableWidgetItem& o) const override {
        return data(Qt::UserRole).toLongLong() < o.data(Qt::UserRole).toLongLong();
    }
};

QString humanSize(qint64 b) {
    const double kb = 1024.0;
    if (b < kb) return QString::number(b) + " B";
    double v = b / kb;
    if (v < kb) return QString::number(v, 'f', 1) + " KB";
    v /= kb;
    if (v < kb) return QString::number(v, 'f', 1) + " MB";
    v /= kb;
    return QString::number(v, 'f', 1) + " GB";
}

} // namespace

DownloadsTab::DownloadsTab(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Name", "Status", "Size", "Downloaded"});
    m_table->setSortingEnabled(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, &DownloadsTab::showContextMenu);
    v->addWidget(m_table, 1);

    auto* bar = new QHBoxLayout;
    auto* refreshBtn = new QPushButton("Refresh", this);
    auto* installBtn = new QPushButton("Install Selected", this);
    bar->addWidget(refreshBtn); bar->addStretch(); bar->addWidget(installBtn);
    v->addLayout(bar);

    connect(refreshBtn, &QPushButton::clicked, this, &DownloadsTab::refresh);

    auto installSelected = [this]{
        int row = m_table->currentRow();
        if (row < 0) return;
        auto* it = m_table->item(row, 0);
        if (it) emit installRequested(it->data(Qt::UserRole).toString());
    };
    connect(installBtn, &QPushButton::clicked, this, installSelected);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [installSelected](int, int){ installSelected(); });

    refresh();
}

void DownloadsTab::refresh() {
    m_activeRows.clear();
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);

    // Collect archive paths that the active profile's mods were installed from.
    QSet<QString> installedSet;
    if (m_profile) {
        for (const ModEntry& e : m_profile->modList())
            if (!e.sourceArchive.isEmpty())
                installedSet.insert(e.sourceArchive);
    }

    QString dir = AppConfig::instance().downloadsDir();
    if (!dir.isEmpty()) {
        QDir d(dir);
        const QFileInfoList files =
            d.entryInfoList({"*.zip","*.7z","*.rar","*.tar","*.gz"}, QDir::Files);
        for (const QFileInfo& fi : files) {
            int row = m_table->rowCount();
            m_table->insertRow(row);

            auto* nameItem = new QTableWidgetItem(fi.fileName());
            nameItem->setData(Qt::UserRole, fi.absoluteFilePath());
            m_table->setItem(row, 0, nameItem);

            const bool installed = installedSet.contains(fi.absoluteFilePath());
            auto* statusItem = new QTableWidgetItem(installed ? "Installed" : "Not installed");
            if (installed) statusItem->setForeground(QColor(0x4c, 0xaf, 0x50));
            m_table->setItem(row, 1, statusItem);

            auto* sizeItem = new NumItem(humanSize(fi.size()));
            sizeItem->setData(Qt::UserRole, fi.size());
            m_table->setItem(row, 2, sizeItem);

            auto* dateItem = new NumItem(fi.lastModified().toString("yyyy-MM-dd hh:mm"));
            dateItem->setData(Qt::UserRole, fi.lastModified().toSecsSinceEpoch());
            m_table->setItem(row, 3, dateItem);
        }
    }

    for (const auto& f : m_failed) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        auto* nameItem = new QTableWidgetItem(f.first);
        nameItem->setData(Qt::UserRole, QString());            // no archive path
        nameItem->setData(Qt::UserRole + 1, QStringLiteral("failed")); // row-kind marker
        m_table->setItem(row, 0, nameItem);
        auto* statusItem = new QTableWidgetItem("Failed: " + f.second);
        statusItem->setForeground(QColor(0xe5, 0x39, 0x35));   // red
        m_table->setItem(row, 1, statusItem);
        m_table->setItem(row, 2, new QTableWidgetItem(QString()));
        m_table->setItem(row, 3, new QTableWidgetItem(QString()));
    }

    m_table->setSortingEnabled(true);
    applyFilters();
}

void DownloadsTab::setDownloadProgress(const QString& fileName, qint64 received, qint64 total) {
    const QString status = (total > 0)
        ? QString("Downloading %1%").arg(int(received * 100 / total))
        : QString("Downloading %1").arg(humanSize(received));

    auto it = m_activeRows.find(fileName);
    if (it != m_activeRows.end()) {
        if (auto* statusItem = m_table->item(it.value(), 1))
            statusItem->setText(status);
        return;
    }

    // Insert a new transient row at the top.
    m_table->setSortingEnabled(false);
    m_table->insertRow(0);

    // Existing tracked rows shift down by one.
    for (auto& row : m_activeRows) ++row;

    auto* nameItem = new QTableWidgetItem(fileName);
    nameItem->setData(Qt::UserRole, QString()); // no path yet
    m_table->setItem(0, 0, nameItem);
    m_table->setItem(0, 1, new QTableWidgetItem(status));
    m_table->setItem(0, 2, new QTableWidgetItem(QString()));
    m_table->setItem(0, 3, new QTableWidgetItem(QString()));

    m_activeRows.insert(fileName, 0);
    m_table->setSortingEnabled(true);
}

void DownloadsTab::setProfile(Profile* profile) {
    m_profile = profile;
    refresh();
}

void DownloadsTab::setFailedDownloads(const QList<QPair<QString,QString>>& failures) {
    m_failed = failures;
    refresh();
}

void DownloadsTab::showContextMenu(const QPoint& pos) {
    QMenu menu(this);

    auto* hideInstalled = menu.addAction("Hide Installed");
    hideInstalled->setCheckable(true);
    hideInstalled->setChecked(m_hideInstalled);
    connect(hideInstalled, &QAction::toggled, this, [this](bool on){
        m_hideInstalled = on;
        applyFilters();
    });

    auto* hideNotInstalled = menu.addAction("Hide Not Installed");
    hideNotInstalled->setCheckable(true);
    hideNotInstalled->setChecked(m_hideNotInstalled);
    connect(hideNotInstalled, &QAction::toggled, this, [this](bool on){
        m_hideNotInstalled = on;
        applyFilters();
    });

    menu.addSeparator();

    // Cancel: enabled only when the selected row is an active download
    // (empty path + status starts with "Downloading"). fileName is col-0 text.
    {
        const int row = m_table->currentRow();
        QString activeFileName;
        if (row >= 0) {
            auto* nameItem = m_table->item(row, 0);
            auto* statusItem = m_table->item(row, 1);
            const bool noPath = nameItem && nameItem->data(Qt::UserRole).toString().isEmpty();
            const bool downloading = statusItem && statusItem->text().startsWith("Downloading");
            if (nameItem && noPath && downloading) activeFileName = nameItem->text();
        }
        auto* cancelAction = menu.addAction("Cancel download");
        cancelAction->setEnabled(!activeFileName.isEmpty());
        connect(cancelAction, &QAction::triggered, this, [this, activeFileName]{
            emit cancelRequested(activeFileName);
        });
    }

    // Retry: enabled only on a failed row (UserRole+1 == "failed").
    {
        const int row = m_table->currentRow();
        QString failedFileName;
        if (row >= 0) {
            auto* nameItem = m_table->item(row, 0);
            if (nameItem && nameItem->data(Qt::UserRole + 1).toString() == "failed")
                failedFileName = nameItem->text();
        }
        auto* retryAction = menu.addAction("Retry download");
        retryAction->setEnabled(!failedFileName.isEmpty());
        connect(retryAction, &QAction::triggered, this, [this, failedFileName]{
            emit retryRequested(failedFileName);
        });
    }

    menu.addSeparator();

    // Collect selected rows' real file paths (skip in-progress rows with empty path).
    QStringList selectedPaths;
    for (const QModelIndex& idx : m_table->selectionModel()->selectedRows()) {
        if (auto* it = m_table->item(idx.row(), 0)) {
            const QString path = it->data(Qt::UserRole).toString();
            if (!path.isEmpty()) selectedPaths.append(path);
        }
    }

    auto* deleteSelected = menu.addAction("Delete Selected");
    deleteSelected->setEnabled(!selectedPaths.isEmpty());
    connect(deleteSelected, &QAction::triggered, this, [this, selectedPaths]{
        const int n = selectedPaths.size();
        if (QMessageBox::question(this, "Delete Downloads",
                QString("Delete %1 download(s) from disk? This removes the archive "
                        "file(s); you can re-download them later.").arg(n))
            != QMessageBox::Yes)
            return;
        for (const QString& p : selectedPaths) QFile::remove(p);
        refresh();
    });

    auto* deleteAll = menu.addAction("Delete All Downloads");
    connect(deleteAll, &QAction::triggered, this, [this]{
        const QString dir = AppConfig::instance().downloadsDir();
        QDir d(dir);
        const QFileInfoList files = dir.isEmpty()
            ? QFileInfoList{}
            : d.entryInfoList({"*.zip","*.7z","*.rar","*.tar","*.gz"}, QDir::Files);
        if (QMessageBox::question(this, "Delete All Downloads",
                QString("Delete ALL %1 downloads in the folder? This removes every "
                        "archive file.").arg(files.size()))
            != QMessageBox::Yes)
            return;
        for (const QFileInfo& fi : files) QFile::remove(fi.absoluteFilePath());
        refresh();
    });

    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void DownloadsTab::applyFilters() {
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* statusItem = m_table->item(row, 1);
        const QString status = statusItem ? statusItem->text() : QString();
        const bool hide = (m_hideInstalled && status == "Installed")
                       || (m_hideNotInstalled && status == "Not installed");
        m_table->setRowHidden(row, hide);
    }
}

} // namespace solero
