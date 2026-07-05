#include "DownloadsTab.h"
#include "core/AppConfig.h"
#include "core/Profile.h"
#include "core/RelativeTime.h"
#include "IconUtil.h"
#include "ElideDelegate.h"
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
#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QResizeEvent>
#include <limits>

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

// The Status column now shows an ICON (+ tooltip) instead of text, so it can no
// longer sort by display text. It stores a rank in Qt::UserRole and sorts on
// that, keeping the underlying-state order the user expects. The status KIND
// string lives in RoleStatusKind and drives filtering + the context menu.
constexpr int RoleStatusKind = Qt::UserRole + 1;

class StatusItem : public QTableWidgetItem {
public:
    bool operator<(const QTableWidgetItem& o) const override {
        return data(Qt::UserRole).toInt() < o.data(Qt::UserRole).toInt();
    }
};

// Build the Status cell for a given kind. `fullText` is the former status string,
// moved to the tooltip; `sideText` is optional short text painted beside the icon
// (only "N%"/size for Downloading). The icon + sort rank are picked per kind. The
// ranks group attention-worthy states first when the user sorts by Status.
QTableWidgetItem* makeStatusItem(const QString& kind, const QString& fullText,
                                 const QString& sideText = QString()) {
    auto* it = new StatusItem();
    it->setText(sideText);
    it->setToolTip(fullText);
    it->setTextAlignment(Qt::AlignCenter);
    it->setData(RoleStatusKind, kind);
    int rank = 4;
    QIcon icon;
    if (kind == QLatin1String("downloading"))       { rank = 0; icon = downloadingStatusIcon(14); }
    else if (kind == QLatin1String("failed"))       { rank = 1; icon = errorSignIcon(14); }
    else if (kind == QLatin1String("not-installed")){ rank = 2; icon = notInstalledStatusIcon(14); }
    else if (kind == QLatin1String("installed"))    { rank = 3; icon = installedStatusIcon(14); }
    else /* cancelled / other terminal-neutral */   { rank = 4; icon = neutralStatusIcon(14); }
    it->setData(Qt::UserRole, rank);
    it->setIcon(icon);
    return it;
}

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
    // Char-level elision for overlong cell text ("Some Mod Na…", not "Some…") on
    // every column - the shared ElideRightDelegate pre-elides with QFontMetrics.
    m_table->setItemDelegate(new ElideRightDelegate(m_table));
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setWordWrap(false);
    // Breathing room between columns so adjacent cells aren't bunched together.
    m_table->setStyleSheet("QTableWidget::item { padding: 2px 8px; }");
    auto* hh = m_table->horizontalHeader();
    hh->setMinimumSectionSize(24);
    // Column-resize model (see ModListView for the proven rationale): every column
    // Interactive + the last section stretches (Downloaded, a right-aligned date, is
    // the tail). The absorbing column is rightmost, so every divider drag follows
    // the cursor, Name is directly resizable, and the columns span the full pane.
    // Status/Size/Downloaded/Name defaults derive from the font (applyColumnWidths)
    // so they scale with Ctrl +/- zoom. They're Interactive (not ResizeToContents)
    // because refresh() runs on every progress tick and ResizeToContents made the
    // columns visibly jump (B-3).
    hh->setSectionResizeMode(QHeaderView::Interactive);
    hh->setStretchLastSection(true);
    applyColumnWidths();
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, &DownloadsTab::showContextMenu);
    v->addWidget(m_table, 1);

    // The persistent Refresh button is gone (the tab already refreshes on change;
    // a manual "Refresh List" also lives in the row context menu). Only the
    // primary "Install Selected" stays on the bar.
    auto* bar = new QHBoxLayout;
    auto* installBtn = new QPushButton("Install Selected", this);
    bar->addStretch(); bar->addWidget(installBtn);
    v->addLayout(bar);

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
            d.entryInfoList({"*.zip","*.7z","*.rar","*.tar","*.gz"}, QDir::Files, QDir::Time);
        for (const QFileInfo& fi : files) {
            int row = m_table->rowCount();
            m_table->insertRow(row);

            auto* nameItem = new QTableWidgetItem(fi.fileName());
            nameItem->setData(Qt::UserRole, fi.absoluteFilePath());
            nameItem->setToolTip(fi.fileName());
            m_table->setItem(row, 0, nameItem);

            const bool installed = installedSet.contains(fi.absoluteFilePath());
            m_table->setItem(row, 1, makeStatusItem(
                installed ? QStringLiteral("installed") : QStringLiteral("not-installed"),
                installed ? QStringLiteral("Installed") : QStringLiteral("Not installed")));

            auto* sizeItem = new NumItem(humanSize(fi.size()));
            sizeItem->setData(Qt::UserRole, fi.size());
            sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(row, 2, sizeItem);

            // Display a relative bucket ("2 hrs ago") but sort on the raw epoch
            // stored in Qt::UserRole (NumItem::operator<), never on the text.
            const qint64 epoch = fi.lastModified().toSecsSinceEpoch();
            auto* dateItem = new NumItem(
                relativeDownloadTime(epoch, QDateTime::currentSecsSinceEpoch()));
            dateItem->setData(Qt::UserRole, epoch);
            dateItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(row, 3, dateItem);
        }
    }

    for (const auto& f : m_failed) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        auto* nameItem = new QTableWidgetItem(f.first);
        nameItem->setData(Qt::UserRole, QString());            // no archive path
        nameItem->setData(Qt::UserRole + 1, QStringLiteral("failed")); // row-kind marker
        nameItem->setToolTip(f.first);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, makeStatusItem(QStringLiteral("failed"), "Failed: " + f.second));
        auto* sz = new NumItem(QString()); sz->setData(Qt::UserRole, qint64(0));
        sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 2, sz);
        // Sentinel just below in-progress so failed rows pin above completed files.
        auto* dt = new NumItem(QString());
        dt->setData(Qt::UserRole, std::numeric_limits<qint64>::max() - 1);
        dt->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 3, dt);
    }

    // Re-enabling sorting re-sorts by the header's current indicator, so the
    // user's chosen sort survives a refresh tick. Only force the newest-first
    // default the first time we populate; afterwards re-assert the current
    // indicator so a mid-download refresh never yanks the sort back to column 3.
    m_table->setSortingEnabled(true);
    auto* hh = m_table->horizontalHeader();
    if (!m_defaultSortApplied) {
        m_table->sortByColumn(3, Qt::DescendingOrder);
        m_defaultSortApplied = true;
    } else {
        m_table->sortByColumn(hh->sortIndicatorSection(), hh->sortIndicatorOrder());
    }
    applyFilters();
}

void DownloadsTab::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::ApplicationFontChange && m_table) {
        // The table carries a direct style sheet (WA_StyleSheet), so Qt won't
        // propagate the new application font to it automatically. Push it in
        // explicitly and re-flow row heights so headers + rows track the zoom.
        const QFont f = QApplication::font();
        m_table->setFont(f);
        m_table->horizontalHeader()->setFont(f);
        m_table->resizeRowsToContents();
        // Column WIDTHS must track the zoom too: the earlier zoom fix only
        // re-asserted the font + row heights, so at higher zoom the text grew but
        // the fixed-pixel columns stayed narrow (worsening elision). Recompute the
        // data columns from the new font's metrics and re-flex Name.
        applyColumnWidths();
    }
}

void DownloadsTab::applyColumnWidths() {
    if (!m_table) return;
    // Derive every column from the current font's metrics (not fixed pixels) so they
    // scale ~proportionally with Ctrl +/- zoom. The last column (Downloaded)
    // stretches to fill, so its width here is just a floor for the overflow case.
    // DownloadsTab doesn't persist header widths; this recomputes on construction
    // and on each zoom step (refresh() never touches widths), so between zooms a
    // user's manual Name resize sticks.
    const QFontMetrics fm(m_table->font());
    auto* hh = m_table->horizontalHeader();
    // ~16px item padding (style sheet) + icon/margins folded into each headroom.
    const int name   = qMax(220, fm.horizontalAdvance(QStringLiteral("A reasonably long example mod file name")) + 20);
    const int status = qMax(56, fm.horizontalAdvance(QStringLiteral("100%")) + 44); // icon + %
    const int size   = qMax(56, fm.horizontalAdvance(QStringLiteral("999.9 MB")) + 20);
    const int dated  = qMax(84, fm.horizontalAdvance(QStringLiteral("00th Sep, 00:00")) + 16);
    hh->resizeSection(0, name);
    hh->resizeSection(1, status);
    hh->resizeSection(2, size);
    hh->resizeSection(3, dated);
    hh->setStretchLastSection(true); // keep the tail filling after the resizes above
}

void DownloadsTab::setDownloadProgress(const QString& fileName, qint64 received, qint64 total) {
    // `side` is the short text painted beside the download icon ("50%" / "12.3 MB");
    // `full` is the full status string, which lives in the cell's tooltip.
    const QString side = (total > 0)
        ? QString("%1%").arg(int(received * 100 / total))
        : humanSize(received);
    const QString full = QStringLiteral("Downloading ") + side;

    auto it = m_activeRows.find(fileName);
    if (it != m_activeRows.end()) {
        if (auto* statusItem = m_table->item(it.value(), 1)) {
            statusItem->setText(side);      // icon already set; just refresh the %
            statusItem->setToolTip(full);
        }
        return;
    }

    // Insert a new transient row at the top.
    m_table->setSortingEnabled(false);
    m_table->insertRow(0);

    // Existing tracked rows shift down by one.
    for (auto& row : m_activeRows) ++row;

    auto* nameItem = new QTableWidgetItem(fileName);
    nameItem->setData(Qt::UserRole, QString()); // no path yet
    nameItem->setToolTip(fileName);
    m_table->setItem(0, 0, nameItem);
    m_table->setItem(0, 1, makeStatusItem(QStringLiteral("downloading"), full, side));
    auto* szItem = new NumItem(QString()); szItem->setData(Qt::UserRole, qint64(0));
    szItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_table->setItem(0, 2, szItem);
    // Max sentinel pins live downloads at the very top of the mtime-descending sort.
    auto* dtItem = new NumItem(QString());
    dtItem->setData(Qt::UserRole, std::numeric_limits<qint64>::max());
    dtItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_table->setItem(0, 3, dtItem);

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

    // Manual refresh (the persistent toolbar button was removed; the list also
    // auto-refreshes when downloads change).
    menu.addAction("Refresh List", this, &DownloadsTab::refresh);
    menu.addSeparator();

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
            const bool downloading = statusItem
                && statusItem->data(RoleStatusKind).toString() == QLatin1String("downloading");
            if (nameItem && noPath && downloading) activeFileName = nameItem->text();
        }
        auto* cancelAction = menu.addAction("Cancel Download");
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
        auto* retryAction = menu.addAction("Retry Download");
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

    auto* deleteSelected = menu.addAction(QStringLiteral("Delete Selected") + QChar(0x2026));
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

    auto* deleteAll = menu.addAction(QStringLiteral("Delete All Downloads") + QChar(0x2026));
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
        const QString kind = statusItem ? statusItem->data(RoleStatusKind).toString() : QString();
        const bool hide = (m_hideInstalled && kind == QLatin1String("installed"))
                       || (m_hideNotInstalled && kind == QLatin1String("not-installed"));
        m_table->setRowHidden(row, hide);
    }
}

} // namespace solero
