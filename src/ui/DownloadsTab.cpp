#include "DownloadsTab.h"
#include "ColumnFit.h"
#include "core/AppConfig.h"
#include "core/Profile.h"
#include "core/RelativeTime.h"
#include "IconUtil.h"
#include "ElideDelegate.h"
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QStyleOptionHeader>
#include <QStyle>
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
#include <QTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <limits>

namespace solero {

namespace {

// A downloads table that draws a centered hint over its viewport when empty, so a
// fresh install doesn't present a blank grid (mirrors the mod/plugin list overlays).
class DownloadsTable : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
protected:
    void paintEvent(QPaintEvent* event) override {
        QTableWidget::paintEvent(event);
        if (rowCount() != 0) return;
        QPainter painter(viewport());
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        const QRect r = viewport()->rect().adjusted(40, 40, -40, -40);
        painter.drawText(r, Qt::AlignCenter | Qt::TextWordWrap,
            QStringLiteral("No downloads yet ") + QChar('-')
                + QStringLiteral(" download from the Nexus browser."));
    }
};

// Centres an icon-only header section (the Status "?" icon) so the header matches
// the centred data cells - a plain header item always draws its icon on the left.
// Other sections paint the normal way.
class CenteredIconHeader : public QHeaderView {
public:
    using QHeaderView::QHeaderView;
protected:
    void paintSection(QPainter* p, const QRect& rect, int idx) const override {
        if (!rect.isValid() || !model()) { QHeaderView::paintSection(p, rect, idx); return; }
        const QString text = model()->headerData(idx, orientation(), Qt::DisplayRole).toString();
        const QVariant decoV = model()->headerData(idx, orientation(), Qt::DecorationRole);
        const QIcon icon = decoV.canConvert<QIcon>() ? decoV.value<QIcon>() : QIcon();
        if (!text.isEmpty() || icon.isNull()) { QHeaderView::paintSection(p, rect, idx); return; }
        // Draw the section chrome with no content, then centre the icon in it.
        QStyleOptionHeader opt;
        initStyleOption(&opt);
        opt.rect = rect;
        opt.section = idx;
        opt.text.clear();
        opt.icon = QIcon();
        opt.iconAlignment = Qt::AlignCenter;
        style()->drawControl(QStyle::CE_Header, &opt, p, this);
        const int sz = 14;
        QRect ir(0, 0, sz, sz);
        ir.moveCenter(rect.center());
        icon.paint(p, ir, Qt::AlignCenter);
    }
};

// Centres the Status column's icon. A plain QTableWidgetItem draws its icon
// left-aligned regardless of textAlignment, so an icon-only status cell looked
// stuck to the left. For icon-only cells this paints the style background (so
// selection still shows) then the icon centred; cells that also carry side text
// (a live download's "rate <dot> ETA") fall back to the normal icon+text layout.
class StatusCellDelegate : public ElideRightDelegate {
public:
    using ElideRightDelegate::ElideRightDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const QString text = index.data(Qt::DisplayRole).toString();
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        if (text.isEmpty() && !icon.isNull()) {
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            opt.icon = QIcon();
            opt.text.clear();
            opt.features &= ~QStyleOptionViewItem::HasDecoration;
            const QWidget* w = opt.widget;
            QStyle* style = w ? w->style() : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w); // bg + selection
            const int sz = 14;
            QRect ir(0, 0, sz, sz);
            ir.moveCenter(option.rect.center());
            icon.paint(painter, ir, Qt::AlignCenter);
            return;
        }
        ElideRightDelegate::paint(painter, option, index);
    }
};

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
    else if (kind == QLatin1String("paused"))       { rank = 0; icon = neutralStatusIcon(14); }
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

// "3.2 MB/s" from a bytes-per-second rate.
QString formatRate(double bytesPerSec) {
    return humanSize(qint64(bytesPerSec)) + "/s";
}

// Seconds to "M:SS" (or "H:MM:SS" past an hour); empty for a nonsensical value.
QString formatEta(qint64 seconds) {
    if (seconds < 0) return QString();
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds % 3600) / 60;
    const qint64 s = seconds % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

} // namespace

DownloadsTab::DownloadsTab(QWidget* parent) : QWidget(parent) {
    m_clock.start(); // monotonic reference for rate/ETA sampling
    auto* v = new QVBoxLayout(this);

    m_table = new DownloadsTable(this);
    m_table->setHorizontalHeader(new CenteredIconHeader(Qt::Horizontal, m_table));
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Name", "Status", "Size", "Downloaded"});
    // The Status cells are icons (see makeStatusItem), so the header shows a "?"
    // icon from the same status-dot family (not the word "Status") that explains
    // itself on hover. Cells are centred by StatusCellDelegate below.
    if (auto* statusHdr = m_table->horizontalHeaderItem(1)) {
        statusHdr->setText(QString());
        statusHdr->setIcon(helpStatusIcon(14));
        statusHdr->setToolTip(QStringLiteral("Status ") + QChar('-')
                              + QStringLiteral(" each mod's download / install state"));
        statusHdr->setTextAlignment(Qt::AlignCenter);
    }
    m_table->setSortingEnabled(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->hide();
    // Char-level elision for overlong cell text ("Some Mod Na…", not "Some…") on
    // every column - the shared ElideRightDelegate pre-elides with QFontMetrics.
    m_table->setItemDelegate(new ElideRightDelegate(m_table));
    m_table->setItemDelegateForColumn(1, new StatusCellDelegate(m_table)); // centre status icons
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
    // Restore persisted widths if the user customised them; otherwise the defaults
    // (content-fit + fill Name) are applied on first show, when the viewport is sized.
    if (const QByteArray st = AppConfig::instance().downloadsHeaderState(); !st.isEmpty()) {
        hh->restoreState(st);
        solero::assertFillMode(hh, /*fillCol=*/0); // Name = Stretch; restoreState can change modes
        m_didAutoSize = true; // keep the restored widths; don't auto-size on first show
    }
    m_headerSaveTimer = new QTimer(this);
    m_headerSaveTimer->setSingleShot(true);
    m_headerSaveTimer->setInterval(300);
    connect(m_headerSaveTimer, &QTimer::timeout, this, &DownloadsTab::saveHeaderState);
    connect(hh, &QHeaderView::sectionResized, this, [this](int,int,int){ m_headerSaveTimer->start(); });
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

    // Coalesce progress ticks: a fast download emits progress on every network read,
    // so repaint the in-progress status cells at most ~8x/sec instead of per chunk.
    m_progressTimer = new QTimer(this);
    m_progressTimer->setSingleShot(true);
    m_progressTimer->setInterval(120);
    connect(m_progressTimer, &QTimer::timeout, this, &DownloadsTab::flushDownloadProgress);

    refresh();
}

void DownloadsTab::refresh() {
    m_activeRows.clear();
    // A full rebuild replaces every in-progress row with its on-disk equivalent, so
    // any queued progress ticks are moot - drop them (flush would no-op anyway).
    m_pendingProgress.clear();
    // Rate samples are tied to transient rows; drop them so a rebuilt row's average
    // starts fresh (it re-accumulates within a few ticks).
    m_rateSamples.clear();
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

    // Paused downloads: persistent rows (the worker emits no ticks while paused, so
    // unlike live downloads they can't rebuild themselves from setDownloadProgress).
    for (auto it = m_pausedDownloads.constBegin(); it != m_pausedDownloads.constEnd(); ++it) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        auto* nameItem = new QTableWidgetItem(it.key());
        nameItem->setData(Qt::UserRole, QString());                     // no archive path yet
        nameItem->setData(Qt::UserRole + 1, QStringLiteral("paused"));  // row-kind marker (menu)
        nameItem->setToolTip(it.key());
        m_table->setItem(row, 0, nameItem);
        const qint64 rec = it.value().first, tot = it.value().second;
        const QString pct = (tot > 0) ? QString("%1%").arg(int(rec * 100 / tot)) : humanSize(rec);
        m_table->setItem(row, 1, makeStatusItem(QStringLiteral("paused"),
            QStringLiteral("Paused at ") + pct, QStringLiteral("Paused ") + pct));
        auto* sz = new NumItem(QString()); sz->setData(Qt::UserRole, qint64(0));
        sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 2, sz);
        // Pin at the top alongside live downloads so a paused item stays visible.
        auto* dt = new NumItem(QString());
        dt->setData(Qt::UserRole, std::numeric_limits<qint64>::max());
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
        // Column WIDTHS should track the zoom too, but only when the user hasn't set
        // their own widths (persisted): re-fitting would clobber a remembered layout.
        if (AppConfig::instance().downloadsHeaderState().isEmpty())
            applyColumnWidths();
    }
}

void DownloadsTab::applyColumnWidths() {
    if (!m_table) return;
    // Content-fit the data columns to the current rows (clamped to sensible floors so
    // an empty/short table still fits the widest values), and give Name (col 0) the
    // remaining width so it's the big default. stretchLastSection keeps Downloaded
    // absorbing later pane-resize slack. Floors are font-derived so they scale with
    // Ctrl +/- zoom. Persisted user widths (restored in the ctor) take precedence.
    const QFontMetrics fm(m_table->font());
    const int nameFloor   = qMax(220, fm.horizontalAdvance(QStringLiteral("A reasonably long example mod file name")) + 20);
    const int statusFloor = qMax(40,  fm.horizontalAdvance(QStringLiteral("100%")) + 30);
    const int sizeFloor   = qMax(56,  fm.horizontalAdvance(QStringLiteral("999.9 MB")) + 20);
    const int datedFloor  = qMax(84,  fm.horizontalAdvance(QStringLiteral("00th Sep, 00:00")) + 16);
    solero::applyFitFillDefaults(m_table, m_table->horizontalHeader(),
                                 /*fillCol=*/0, {nameFloor, statusFloor, sizeFloor, datedFloor});
}

void DownloadsTab::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // First real show: apply the content-fit + fill-Name defaults now that the table
    // has a width. Skipped when persisted widths were restored (m_didAutoSize set).
    if (!m_didAutoSize) {
        m_didAutoSize = true;
        QTimer::singleShot(0, this, [this]{ applyColumnWidths(); });
    }
}

void DownloadsTab::saveHeaderState() {
    if (!m_table) return;
    AppConfig::instance().setDownloadsHeaderState(m_table->horizontalHeader()->saveState());
    AppConfig::instance().save();
}

void DownloadsTab::setDownloadProgress(const QString& fileName, qint64 received, qint64 total) {
    // Paused: ignore any in-flight ticks still crossing the thread boundary, so a
    // late tick can't resurrect a live "downloading" row beside the paused one.
    if (m_pausedDownloads.contains(fileName)) return;

    // Record a rate sample and the latest bytes (the latter is the snapshot used if
    // this download is paused). Trim the window to ~3s of history for the average.
    const qint64 nowMs = m_clock.elapsed();
    auto& samples = m_rateSamples[fileName];
    samples.append({nowMs, received});
    constexpr qint64 kRateWindowMs = 3000;
    while (samples.size() > 2 && (nowMs - samples.first().first) > kRateWindowMs)
        samples.removeFirst();
    m_latestProgress.insert(fileName, {received, total});

    // Fast path: the row already exists, so just stash the latest bytes and let the
    // timer coalesce the (frequent) mid-download ticks into a single repaint.
    auto it = m_activeRows.find(fileName);
    if (it != m_activeRows.end()) {
        m_pendingProgress.insert(fileName, {received, total});
        if (!m_progressTimer->isActive()) m_progressTimer->start();
        return;
    }

    // First tick for this file: build its transient row immediately (structural change).
    // `side` is the short text painted beside the download icon ("3.2 MB/s <dot> 0:45"
    // once a rate is known, else "50%"); `full` is the full status tooltip.
    const auto [side, full] = formatActiveProgress(fileName, received, total);

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

double DownloadsTab::currentRate(const QString& fileName) const {
    auto it = m_rateSamples.constFind(fileName);
    if (it == m_rateSamples.constEnd() || it->size() < 2) return 0.0;
    const QPair<qint64,qint64>& first = it->first();
    const QPair<qint64,qint64>& last  = it->last();
    const qint64 dtMs = last.first - first.first;   // elapsed ms across the window
    const qint64 dBytes = last.second - first.second;
    if (dtMs <= 0 || dBytes <= 0) return 0.0;
    return double(dBytes) * 1000.0 / double(dtMs);  // bytes/sec
}

QPair<QString,QString> DownloadsTab::formatActiveProgress(const QString& fileName,
                                                          qint64 received, qint64 total) const {
    const QString pct = (total > 0)
        ? QString("%1%").arg(int(received * 100 / total))
        : humanSize(received);
    const double rate = currentRate(fileName);
    if (rate <= 0.0)
        return {pct, QStringLiteral("Downloading ") + pct};

    const QString rateStr = formatRate(rate);
    QString etaStr;
    if (total > 0 && received <= total)
        etaStr = formatEta(qint64(double(total - received) / rate));

    // Beside the icon: "3.2 MB/s <middle-dot> 0:45" (drop the ETA if unknown).
    QString side = rateStr;
    if (!etaStr.isEmpty())
        side += QStringLiteral(" ") + QChar(0x00B7) + QStringLiteral(" ") + etaStr;
    // Tooltip carries the percentage too, which the narrow cell can't always show.
    QString full = QStringLiteral("Downloading ") + pct + QStringLiteral(" ")
                 + QChar('-') + QStringLiteral(" ") + rateStr;
    if (!etaStr.isEmpty())
        full += QStringLiteral(", ") + etaStr + QStringLiteral(" left");
    return {side, full};
}

void DownloadsTab::flushDownloadProgress() {
    for (auto it = m_pendingProgress.constBegin(); it != m_pendingProgress.constEnd(); ++it) {
        auto rowIt = m_activeRows.constFind(it.key());
        if (rowIt == m_activeRows.constEnd()) continue; // row rebuilt away meanwhile
        const auto [side, full] = formatActiveProgress(it.key(), it.value().first, it.value().second);
        if (auto* statusItem = m_table->item(rowIt.value(), 1)) {
            statusItem->setText(side);      // icon already set; just refresh the %
            statusItem->setToolTip(full);
        }
    }
    m_pendingProgress.clear();
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

    // Pause/Resume/Cancel: classify the selected row. A live download has an empty
    // path + "downloading" status; a held one has "paused" status. fileName is col-0.
    {
        const int row = m_table->currentRow();
        QString activeFileName, pausedFileName;
        if (row >= 0) {
            auto* nameItem = m_table->item(row, 0);
            auto* statusItem = m_table->item(row, 1);
            const QString kind = statusItem ? statusItem->data(RoleStatusKind).toString() : QString();
            const bool noPath = nameItem && nameItem->data(Qt::UserRole).toString().isEmpty();
            if (nameItem && noPath && kind == QLatin1String("downloading"))
                activeFileName = nameItem->text();
            if (nameItem && kind == QLatin1String("paused"))
                pausedFileName = nameItem->text();
        }

        auto* pauseAction = menu.addAction("Pause Download");
        pauseAction->setEnabled(!activeFileName.isEmpty());
        connect(pauseAction, &QAction::triggered, this, [this, activeFileName]{
            // Snapshot the latest bytes so the persistent paused row shows progress.
            m_pausedDownloads.insert(activeFileName,
                                     m_latestProgress.value(activeFileName, {0, 0}));
            m_rateSamples.remove(activeFileName);
            emit pauseRequested(activeFileName);
            refresh(); // swap the live row for the persistent "Paused" row
        });

        auto* resumeAction = menu.addAction("Resume Download");
        resumeAction->setEnabled(!pausedFileName.isEmpty());
        connect(resumeAction, &QAction::triggered, this, [this, pausedFileName]{
            m_pausedDownloads.remove(pausedFileName);
            emit resumeRequested(pausedFileName);
            refresh(); // row reappears as "Downloading" on the next progress tick
        });

        // Cancel works for either a live or a paused download.
        const QString cancelTarget = !activeFileName.isEmpty() ? activeFileName : pausedFileName;
        auto* cancelAction = menu.addAction("Cancel Download");
        cancelAction->setEnabled(!cancelTarget.isEmpty());
        connect(cancelAction, &QAction::triggered, this, [this, cancelTarget]{
            m_pausedDownloads.remove(cancelTarget); // no-op if it was live
            m_rateSamples.remove(cancelTarget);
            emit cancelRequested(cancelTarget);
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

    // Enumerate the folder up-front so "Delete All" can disable itself at zero
    // downloads (like "Delete Selected"), rather than opening an empty confirm.
    const QString allDir = AppConfig::instance().downloadsDir();
    const QFileInfoList allFiles = allDir.isEmpty()
        ? QFileInfoList{}
        : QDir(allDir).entryInfoList({"*.zip","*.7z","*.rar","*.tar","*.gz"}, QDir::Files);

    auto* deleteAll = menu.addAction(QStringLiteral("Delete All Downloads") + QChar(0x2026));
    deleteAll->setEnabled(!allFiles.isEmpty());
    connect(deleteAll, &QAction::triggered, this, [this, allFiles]{
        const QFileInfoList& files = allFiles;
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
