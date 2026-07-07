#include "SavesTab.h"
#include "saves/SaveFile.h"
#include "core/AppConfig.h"
#include <QEvent>
#include <QTimer>
#include "ColumnFit.h"
#include "core/Profile.h"
#include "core/PluginList.h"
#include "IconUtil.h"
#include "ElideDelegate.h"
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QSplitter>
#include <QScrollArea>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QImage>
#include <QPixmap>
#include <QIcon>
#include <QHash>
#include <QDateTime>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QShowEvent>

namespace solero {

namespace {

// Column layout for the saves table.
enum Col { ColShot = 0, ColCharacter, ColLevel, ColLocation, ColDate, ColSaveNum, ColCount };

// Thumbnail size for the screenshot column (kept modest so a long list stays light).
constexpr int kThumbW = 96;
constexpr int kThumbH = 54;

// Sorts by the numeric value stashed in Qt::UserRole rather than the display text,
// so Level / Save # order 2 < 10 (mirrors DownloadsTab's NumItem).
class NumItem : public QTableWidgetItem {
public:
    explicit NumItem(const QString& text) : QTableWidgetItem(text) {}
    bool operator<(const QTableWidgetItem& o) const override {
        return data(Qt::UserRole).toLongLong() < o.data(Qt::UserRole).toLongLong();
    }
};

// A saves table that draws a centered hint over its viewport when empty, so a
// fresh install / no-saves state doesn't present a blank grid.
class SavesTable : public QTableWidget {
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
            QStringLiteral("No saves found ") + QChar('-')
                + QStringLiteral(" play the game to create one."));
    }
};

// Build a QImage from a save's raw screenshot bytes. SSE screenshots are RGBA
// (4 bpp), LE are RGB (3 bpp); the alpha channel is ignored (RGBX) so a zero-alpha
// SSE shot doesn't render transparent. Returns a null image when data is absent.
QImage screenshotImage(const SaveHeader& h) {
    if (h.screenshotWidth == 0 || h.screenshotHeight == 0 || h.screenshotRgb.isEmpty())
        return {};
    const int w = static_cast<int>(h.screenshotWidth);
    const int hgt = static_cast<int>(h.screenshotHeight);
    const int bpp = h.screenshotBytesPerPixel;
    const QImage::Format fmt = (bpp == 4) ? QImage::Format_RGBX8888 : QImage::Format_RGB888;
    // Copy so the QImage owns its bytes (QByteArray is a local in the caller).
    QImage img(reinterpret_cast<const uchar*>(h.screenshotRgb.constData()),
               w, hgt, w * bpp, fmt);
    return img.copy();
}

// Per-path parse cache entry: keeps a save's parsed header keyed by its mtime so a
// repeat refresh only re-parses files that actually changed.
struct CacheEntry {
    qint64 mtime = 0;
    SaveHeader header;
};

} // namespace

// A parse cache lives for the tab's lifetime; declared here to keep the header light.
static QHash<QString, CacheEntry>& parseCache() {
    static QHash<QString, CacheEntry> cache;
    return cache;
}

SavesTab::SavesTab(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);

    m_table = new SavesTable(this);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels(
        {QString(), "Character", "Level", "Location", "Date", "Save #"});
    m_table->setSortingEnabled(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); // read-only: no edits
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->hide();
    m_table->setIconSize(QSize(kThumbW, kThumbH));
    m_table->setItemDelegate(new ElideRightDelegate(m_table));
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setWordWrap(false);
    m_table->setStyleSheet("QTableWidget::item { padding: 2px 8px; }");

    auto* hh = m_table->horizontalHeader();
    hh->setSectionResizeMode(QHeaderView::Interactive);
    hh->setStretchLastSection(true); // Save # (last) absorbs pane-resize slack
    hh->setMinimumSectionSize(24);
    if (const QByteArray st = AppConfig::instance().savesHeaderState(); !st.isEmpty()) {
        hh->restoreState(st);
        hh->setSectionResizeMode(QHeaderView::Interactive); // restoreState can change modes
        hh->setStretchLastSection(true);
        m_didAutoSize = true; // keep restored widths; skip fit-fill defaults on show
    } else {
        // Pre-show fallback; the real content-fit + fill-Location runs on first show.
        hh->resizeSection(ColShot, kThumbW + 12);
        hh->resizeSection(ColCharacter, 160);
        hh->resizeSection(ColLevel, 60);
        hh->resizeSection(ColLocation, 240);
        hh->resizeSection(ColDate, 150);
        hh->resizeSection(ColSaveNum, 70);
    }
    m_hdrSaveTimer = new QTimer(this);
    m_hdrSaveTimer->setSingleShot(true);
    m_hdrSaveTimer->setInterval(300);
    connect(m_hdrSaveTimer, &QTimer::timeout, this, [this] {
        if (m_table) {
            AppConfig::instance().setSavesHeaderState(m_table->horizontalHeader()->saveState());
            AppConfig::instance().save();
        }
    });
    connect(hh, &QHeaderView::sectionResized, this, [this](int, int, int){ m_hdrSaveTimer->start(); });

    // Previewer (MO2-style): the list on the left, a save-detail panel on the right,
    // split by a draggable divider. The panel shows the selected save's screenshot,
    // metadata, and any plugins it needs that the profile's load order lacks.
    auto* preview = new QWidget(this);
    auto* pv = new QVBoxLayout(preview);
    pv->setContentsMargins(10, 6, 6, 6);
    m_previewShot = new QLabel(preview);
    m_previewShot->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_previewShot->setMinimumHeight(kThumbH);
    m_previewShot->installEventFilter(this); // rescale the screenshot when the pane resizes
    pv->addWidget(m_previewShot);
    m_previewInfo = new QLabel(preview);
    m_previewInfo->setTextFormat(Qt::RichText);
    m_previewInfo->setWordWrap(true);
    m_previewInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_previewInfo->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    pv->addWidget(m_previewInfo);
    m_previewMissing = new QLabel(preview);
    m_previewMissing->setTextFormat(Qt::RichText);
    m_previewMissing->setWordWrap(true);
    m_previewMissing->setVisible(false);
    pv->addWidget(m_previewMissing);
    pv->addStretch(1);

    auto* previewScroll = new QScrollArea(this);
    previewScroll->setWidget(preview);
    previewScroll->setWidgetResizable(true);
    previewScroll->setFrameShape(QFrame::NoFrame);

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(m_table);
    split->addWidget(previewScroll);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({560, 360});
    v->addWidget(split, 1);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &SavesTab::updatePreview);

    auto* bar = new QHBoxLayout;
    m_countLabel = new QLabel(this);
    bar->addWidget(m_countLabel);
    bar->addStretch();
    // per-profile saves toggle. Takes effect on the next Deploy (the
    // redirect is written into the deployed Skyrim.ini's SLocalSavePath).
    m_localSavesCheck = new QCheckBox("Profile-specific saves", this);
    m_localSavesCheck->setToolTip(
        "Give this profile its own Saves folder (Saves/<profile>). Applied on the "
        "next Deploy, then when you next launch the game.");
    connect(m_localSavesCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_profile) return;
        if (m_profile->localSaves() == on) return;
        m_profile->setLocalSaves(on);
        m_profile->save();
        rebuild();
    });
    bar->addWidget(m_localSavesCheck);
    auto* refreshBtn = new QPushButton("Refresh", this);
    refreshBtn->setToolTip("Rescan the Skyrim Saves folder");
    connect(refreshBtn, &QPushButton::clicked, this, &SavesTab::refresh);
    bar->addWidget(refreshBtn);
    v->addLayout(bar);
}

void SavesTab::setProfile(Profile* profile) {
    m_profile = profile;
    if (m_localSavesCheck) {
        QSignalBlocker block(m_localSavesCheck); // don't re-save while syncing UI
        m_localSavesCheck->setChecked(profile && profile->localSaves());
    }
    rebuild();
}

void SavesTab::refresh() {
    rebuild();
}

void SavesTab::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // First real show: content-fit the columns, with Location taking the remaining
    // width. Skipped when persisted widths were restored.
    if (!m_didAutoSize) {
        m_didAutoSize = true;
        QTimer::singleShot(0, this, [this] {
            solero::applyFitFillDefaults(m_table, m_table->horizontalHeader(),
                                         ColLocation, {kThumbW + 12, 120, 44, 200, 130, 60});
            m_table->horizontalHeader()->setStretchLastSection(true);
        });
    }
    rebuild();
}

bool SavesTab::eventFilter(QObject* obj, QEvent* e) {
    if (obj == m_previewShot && e->type() == QEvent::Resize)
        rescaleShot();
    return QWidget::eventFilter(obj, e);
}

void SavesTab::rescaleShot() {
    if (!m_previewShot || m_curShot.isNull()) return;
    const int w = qMax(200, m_previewShot->width() - 8);
    if (w == m_lastShotW) return; // avoid redundant re-scales / layout feedback
    m_lastShotW = w;
    // Expand to the panel width (no fixed cap), but don't upscale a tiny screenshot
    // past a sane multiple of its source so it doesn't turn to mush.
    const int maxW = qMax(m_curShot.width() * 4, 700);
    m_previewShot->setPixmap(QPixmap::fromImage(m_curShot).scaledToWidth(
        qMin(w, maxW), Qt::SmoothTransformation));
}

void SavesTab::rebuild() {
    if (!m_table) return;

    // The active profile's plugin filenames (present in the load order), for
    // missing-plugin detection.
    QStringList loadOrder;
    if (m_profile) {
        const PluginList& pl = m_profile->pluginList();
        for (int i = 0; i < pl.count(); ++i)
            loadOrder << pl.at(i).filename;
    }

    // when the active profile uses per-profile saves, read its own
    // Saves/<name> subfolder (where the deploy-time SLocalSavePath points the game).
    QString dir = AppConfig::instance().savesDir();
    if (m_profile && m_profile->localSaves() && !dir.isEmpty())
        dir += "/" + m_profile->saveFolderName();
    QFileInfoList files;
    if (!dir.isEmpty())
        files = QDir(dir).entryInfoList({"*.ess"}, QDir::Files, QDir::Time);

    // Drop cache entries whose save no longer exists on disk.
    {
        QSet<QString> live;
        for (const QFileInfo& fi : files) live.insert(fi.absoluteFilePath());
        auto& cache = parseCache();
        for (auto it = cache.begin(); it != cache.end();) {
            if (!live.contains(it.key())) it = cache.erase(it);
            else ++it;
        }
    }

    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);

    int flagged = 0;
    for (const QFileInfo& fi : files) {
        const QString path = fi.absoluteFilePath();
        const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();

        // Serve unchanged saves from the parse cache; parse only new/changed files.
        auto& cache = parseCache();
        auto cit = cache.constFind(path);
        SaveHeader h;
        if (cit != cache.constEnd() && cit->mtime == mtime) {
            h = cit->header;
        } else {
            h = parseSaveHeader(path);
            cache.insert(path, CacheEntry{mtime, h});
        }
        if (!h.ok) continue; // unparseable save - skip rather than show a broken row

        const int row = m_table->rowCount();
        m_table->insertRow(row);

        // Screenshot thumbnail (col 0). NumItem keeps the row sortable by mtime when
        // the user clicks this column's header; UserRole carries the file path.
        auto* shotItem = new NumItem(QString());
        shotItem->setData(Qt::UserRole, fi.lastModified().toSecsSinceEpoch());
        shotItem->setData(Qt::UserRole + 1, path);
        const QImage img = screenshotImage(h);
        if (!img.isNull()) {
            const QPixmap pm = QPixmap::fromImage(img).scaled(
                kThumbW, kThumbH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            shotItem->setIcon(QIcon(pm));
        }
        m_table->setItem(row, ColShot, shotItem);

        auto* nameItem = new QTableWidgetItem(
            h.characterName.isEmpty() ? fi.completeBaseName() : h.characterName);
        nameItem->setToolTip(fi.fileName());

        // Missing-plugin flag: warn when a readable save references plugins that the
        // active profile's load order lacks. Unreadable plugin lists (compressed
        // saves we couldn't decode) are noted quietly, not flagged as broken.
        const QStringList missing = missingPlugins(h, loadOrder);
        if (!missing.isEmpty()) {
            ++flagged;
            nameItem->setIcon(warnSignIcon(14));
            QString tip = QStringLiteral("This save references ")
                + QString::number(missing.size())
                + QStringLiteral(" plugin(s) not in the current load order:\n  ")
                + missing.join(QStringLiteral("\n  "));
            nameItem->setToolTip(tip);
        } else if (m_profile && !h.pluginsReadable && h.compressionType != 0) {
            nameItem->setToolTip(fi.fileName()
                + QStringLiteral("\n(compressed save - plugin list not readable)"));
        }
        m_table->setItem(row, ColCharacter, nameItem);

        auto* levelItem = new NumItem(QString::number(h.level));
        levelItem->setData(Qt::UserRole, static_cast<qlonglong>(h.level));
        levelItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, ColLevel, levelItem);

        auto* locItem = new QTableWidgetItem(h.location);
        locItem->setToolTip(h.location);
        m_table->setItem(row, ColLocation, locItem);

        // Show the real-world save time (from the file mtime); sort on the epoch.
        auto* dateItem = new NumItem(
            fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        dateItem->setData(Qt::UserRole, fi.lastModified().toSecsSinceEpoch());
        dateItem->setToolTip(h.gameDate); // in-game date as a secondary detail
        m_table->setItem(row, ColDate, dateItem);

        auto* numItem = new NumItem(QString::number(h.saveNumber));
        numItem->setData(Qt::UserRole, static_cast<qlonglong>(h.saveNumber));
        numItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, ColSaveNum, numItem);
    }

    m_table->setSortingEnabled(true);
    // Default: newest save first.
    m_table->sortByColumn(ColDate, Qt::DescendingOrder);
    m_table->resizeRowsToContents();

    QString summary = QString::number(m_table->rowCount()) + QStringLiteral(" save(s)");
    if (flagged > 0)
        summary += QStringLiteral(" ") + QChar('-') + QStringLiteral(" ")
                 + QString::number(flagged) + QStringLiteral(" need missing plugins");
    m_countLabel->setText(summary);

    // Default to the newest save selected (MO2-style), so the previewer shows
    // something instead of an empty panel.
    if (m_table->selectionModel() && m_table->selectionModel()->selectedRows().isEmpty()
        && m_table->rowCount() > 0)
        m_table->selectRow(0);

    updatePreview(); // refresh the panel for the (possibly changed) selection
}

void SavesTab::updatePreview() {
    if (!m_previewInfo) return;

    // The selected save's file path is stashed on the screenshot cell (UserRole+1).
    QString path;
    const auto sel = m_table->selectionModel() ? m_table->selectionModel()->selectedRows(ColShot)
                                               : QModelIndexList();
    if (!sel.isEmpty())
        path = sel.first().data(Qt::UserRole + 1).toString();

    if (path.isEmpty()) {
        m_curShot = QImage();
        m_previewShot->clear();
        m_previewInfo->setText(QStringLiteral(
            "<span style='color:#888;'>Select a save to preview.</span>"));
        m_previewMissing->setVisible(false);
        return;
    }

    // Reuse the parse cache (the row was parsed during rebuild).
    const auto cit = parseCache().constFind(path);
    const SaveHeader h = cit != parseCache().constEnd() ? cit->header : parseSaveHeader(path);

    // Large screenshot, scaled to the panel width (aspect-kept).
    const QImage img = screenshotImage(h);
    m_curShot = img;   // remembered so the pane-resize filter can rescale it
    m_lastShotW = -1;  // force a fresh scale for this save
    if (!img.isNull()) {
        rescaleShot();
    } else {
        m_previewShot->setText(QStringLiteral("<span style='color:#888;'>(no screenshot)</span>"));
    }

    const QString savedWhen =
        QFileInfo(path).lastModified().toString(QStringLiteral("dd MMM yyyy, HH:mm"));
    m_previewInfo->setText(saveSummaryHtml(h, savedWhen));

    // "This save needs plugins you don't have" - the same set the list flags.
    QStringList loadOrder;
    if (m_profile) {
        const PluginList& pl = m_profile->pluginList();
        for (int i = 0; i < pl.count(); ++i) loadOrder << pl.at(i).filename;
    }
    const QStringList missing = missingPlugins(h, loadOrder);
    if (missing.isEmpty()) {
        m_previewMissing->setVisible(false);
    } else {
        // Escape each name first, then join with <br> (escaping the joined string
        // would turn the separators into literal "&lt;br&gt;"). Cap the list so a
        // heavily-diverged save doesn't produce a wall of text.
        constexpr int kMax = 20;
        QStringList shown;
        for (int i = 0; i < missing.size() && i < kMax; ++i)
            shown << missing.at(i).toHtmlEscaped();
        QString body = shown.join(QStringLiteral("<br>"));
        if (missing.size() > kMax)
            body += QStringLiteral("<br>") + QChar(0x2026)
                  + QStringLiteral(" and %1 more").arg(missing.size() - kMax);
        m_previewMissing->setText(QStringLiteral(
            "<div style='margin-top:8px;'><b style='color:#e08a2e;'>Missing plugins "
            "(%1)</b><br><span style='color:#c0653a;'>%2</span></div>")
            .arg(missing.size()).arg(body));
        m_previewMissing->setVisible(true);
    }
}

} // namespace solero
