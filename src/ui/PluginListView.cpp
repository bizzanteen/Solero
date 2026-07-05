#include "PluginListView.h"
#include "PluginListModel.h"
#include "install/PluginScanner.h"
#include "core/AppConfig.h"
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QItemSelectionModel>
#include <QSet>
#include <QMenu>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QShowEvent>
#include <QTimer>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QStyle>
#include <QPainter>
#include <QIcon>
#include "ElideDelegate.h"
namespace solero {

namespace {
// The model exposes the missing-master warning via Qt::DecorationRole on the
// Plugin column. The default delegate draws that icon to the LEFT of the name;
// instead, suppress the leading icon and paint it immediately after the text so
// the warning trails the plugin name. The model's tooltip (listing the missing
// masters) is left untouched, so hovering still works.
class NameDelegate : public ElideRightDelegate {
public:
    using ElideRightDelegate::ElideRightDelegate;
protected:
    void initStyleOption(QStyleOptionViewItem* opt, const QModelIndex& idx) const override {
        QStyledItemDelegate::initStyleOption(opt, idx);
        const bool hasWarn = !qvariant_cast<QIcon>(idx.data(Qt::DecorationRole)).isNull();
        // Drop the leading decoration so the base delegate doesn't draw it on the
        // left; we repaint it after the text in paint().
        opt->icon = QIcon();
        opt->features &= ~QStyleOptionViewItem::HasDecoration;
        // Reserve room for the trailing warning icon (icon + gap) before eliding,
        // so a long name shortens enough to leave the icon visible instead of
        // being clipped off the right edge.
        if (hasWarn) {
            const int sz = qMin(opt->rect.height(), 16);
            opt->rect.setRight(opt->rect.right() - (sz + 4)); // sz + gap
        }
        // Elide the plugin name char-level after the decoration is removed, so the
        // text rect reflects the reserved (no leading icon) width.
        elideRight(opt);
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& idx) const override {
        QStyledItemDelegate::paint(painter, option, idx);
        const QIcon icon = qvariant_cast<QIcon>(idx.data(Qt::DecorationRole));
        if (icon.isNull()) return;
        // Locate where the (left-aligned) text ends, using the same font the base
        // delegate used (FontRole-aware, so bold ESM widths are correct).
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, idx);
        const QWidget* w = opt.widget;
        QStyle* style = w ? w->style() : QApplication::style();
        // textRect already excludes the reserved icon strip (see initStyleOption),
        // so its right edge marks where the elided text ends at the latest.
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, w);
        const QString text = idx.data(Qt::DisplayRole).toString();
        const int textW = opt.fontMetrics.horizontalAdvance(text);
        const int sz = qMin(textRect.height(), 16);
        const int gap = 4;
        // Hug the text for short names; for long (elided) names clamp into the
        // reserved strip just past textRect so the icon always fits the cell.
        const int x = qMin(textRect.left() + textW + gap, textRect.right() + gap);
        const int y = textRect.top() + (textRect.height() - sz) / 2;
        icon.paint(painter, QRect(x, y, sz, sz), Qt::AlignCenter);
    }
};
} // namespace
PluginListView::PluginListView(QWidget* parent) : QTableView(parent) {
    m_model = new PluginListModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    setModel(m_model);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDragDropOverwriteMode(false);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    horizontalHeader()->setSectionsClickable(true);
    horizontalHeader()->setSortIndicatorShown(true);
    horizontalHeader()->setSortIndicator(PluginListModel::ColPriority, Qt::AscendingOrder);
    // Draw the missing-master warning icon after the plugin name (see NameDelegate).
    setItemDelegateForColumn(PluginListModel::ColName, new NameDelegate(this));
    // Char-level elision for long plugin names ("SomePlugin - Pat…", not "SomePlugin…").
    setTextElideMode(Qt::ElideRight);
    setWordWrap(false);
    applyHeaderLayout();
    verticalHeader()->hide();
    // Debounce column-resize saves (B-1): coalesce a resize drag into one config
    // write 300ms after the user stops. The header object persists across model
    // swaps, so this single connection stays valid.
    m_headerSaveTimer = new QTimer(this);
    m_headerSaveTimer->setSingleShot(true);
    m_headerSaveTimer->setInterval(300);
    connect(m_headerSaveTimer, &QTimer::timeout, this, &PluginListView::saveHeaderState);
    connect(horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int, int, int){ m_headerSaveTimer->start(); });
    connect(horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &PluginListView::onSortChanged);
    connect(m_model, &PluginListModel::loadOrderChanged,
            this, &PluginListView::loadOrderChanged);
    connect(m_model, &PluginListModel::pluginEnabledChanged,
            this, &PluginListView::pluginEnabledChanged);
}

void PluginListView::applyHeaderLayout() {
    // Column-resize model (see ModListView for the proven rationale): every column
    // Interactive + the last section stretches (Flags is the tail). The absorbing
    // column is rightmost, so every divider drag follows the cursor, Name is
    // directly resizable, and the columns always span the full pane.
    auto* hh = horizontalHeader();
    hh->setSectionResizeMode(QHeaderView::Interactive);
    hh->setStretchLastSection(true);
    hh->setMinimumSectionSize(22); // guard against unusably narrow columns (B-5)
    hh->resizeSection(PluginListModel::ColEnabled, 28);
    hh->resizeSection(PluginListModel::ColPriority, 40);
    hh->resizeSection(PluginListModel::ColName, 260);
    // Explicit width on the stretch tail (Flags) so it shrinks smoothly when the
    // user widens Name, rather than pinning and jumping to a scrollbar. Stretch
    // still grows it to fill any slack.
    hh->resizeSection(PluginListModel::ColFlags, 50);
    // Restore persisted column widths (B-1). applyHeaderLayout re-runs on every
    // model swap (filter on/off), so re-apply the saved blob each time or a filter
    // toggle would reset the user's widths back to the defaults above.
    const QByteArray saved = AppConfig::instance().pluginListHeaderState();
    if (!saved.isEmpty()) hh->restoreState(saved);
    // restoreState also restores resize MODES / stretch flag; re-assert the proven
    // layout so an older blob can't reinstate the inverted-drag behaviour.
    hh->setSectionResizeMode(QHeaderView::Interactive);
    hh->setStretchLastSection(true);
}

void PluginListView::saveHeaderState() {
    AppConfig::instance().setPluginListHeaderState(horizontalHeader()->saveState());
    AppConfig::instance().save();
}

void PluginListView::autoSizeColumns() {
    horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
    horizontalHeader()->setStretchLastSection(true); // resizeSections can clear it
}

void PluginListView::showEvent(QShowEvent* event) {
    QTableView::showEvent(event);
    // Auto-fit on first show only when there are no persisted widths (B-1/B-2);
    // otherwise applyHeaderLayout already restored the user's columns.
    if (!m_didAutoSize) {
        m_didAutoSize = true;
        if (AppConfig::instance().pluginListHeaderState().isEmpty())
            QTimer::singleShot(0, this, [this]{ autoSizeColumns(); });
    }
}

void PluginListView::setFilter(const QString& text) {
    const QString t = text.trimmed();
    m_filterActive = !t.isEmpty();
    if (m_filterActive) {
        if (model() != m_proxy) {
            m_proxy->setSourceModel(m_model);
            setModel(m_proxy);
            applyHeaderLayout();
        }
        m_proxy->setFilterKeyColumn(PluginListModel::ColName);
        m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        m_proxy->setFilterFixedString(t);
        // Reordering a filtered subset is meaningless - suspend drag-and-drop.
        setDragDropMode(QAbstractItemView::NoDragDrop);
        setDragEnabled(false);
        setAcceptDrops(false);
    } else {
        // Restore the priority-sorted source model + manual drag-reorder.
        m_proxy->setFilterFixedString(QString());
        if (model() != m_model) { setModel(m_model); applyHeaderLayout(); }
        setDragDropMode(QAbstractItemView::InternalMove);
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropOverwriteMode(false);
        setDefaultDropAction(Qt::MoveAction);
        horizontalHeader()->setSortIndicator(PluginListModel::ColPriority, Qt::AscendingOrder);
    }
}

void PluginListView::onSortChanged(int col, Qt::SortOrder order) {
    if (col == PluginListModel::ColPriority) {
        if (!m_filterActive) {
            if (model() != m_model) { setModel(m_model); applyHeaderLayout(); }
            setDragDropMode(QAbstractItemView::InternalMove);
            setDragEnabled(true); setAcceptDrops(true);
            setDropIndicatorShown(true);
            setDragDropOverwriteMode(false);
            setDefaultDropAction(Qt::MoveAction);
        }
    } else {
        if (model() != m_proxy) {
            m_proxy->setSourceModel(m_model);
            setModel(m_proxy);
            applyHeaderLayout();
        }
        m_proxy->sort(col, order);
        setDragDropMode(QAbstractItemView::NoDragDrop);
        setDragEnabled(false); setAcceptDrops(false);
    }
}
void PluginListView::setProfile(Profile* profile) {
    m_model->setProfile(profile);
    // Auto-fit columns to content only once, and only when the user has no saved
    // widths to honour (B-2). Previously this fired on every profile switch and
    // clobbered manual resizes.
    QTimer::singleShot(0, this, [this]{
        if (!m_didAutoSize && AppConfig::instance().pluginListHeaderState().isEmpty()) {
            autoSizeColumns();
            m_didAutoSize = true;
        }
    });
}

void PluginListView::reconcileWith(Profile* profile, const QString& stagingRoot) {
    m_model->setProfile(profile);
    if (profile) {
        // Plugins available to this profile = base/official + whatever's in the
        // live Data, UNION the plugins provided by this profile's enabled staged
        // mods. Without the staged half, an imported-but-not-yet-deployed profile
        // loses every mod plugin (their ESPs aren't in live Data yet), so deploy
        // would write a vanilla-only Plugins.txt and the game would load no mods.
        auto available = PluginScanner::scanGameData(AppConfig::instance().gameDir());
        const QStringList staged = PluginScanner::scan(profile->modList(), stagingRoot);
        for (const QString& p : staged)
            if (!available.contains(p, Qt::CaseInsensitive)) available << p;
        // Snapshot the plugin list (filename + enabled + order) before reconcile so
        // we only pay for a profile save when the list actually changed.
        const auto snapshot = [&] {
            QStringList s;
            const PluginList& pl = profile->pluginList();
            for (int i = 0; i < pl.count(); ++i) {
                const auto& p = pl.at(i);
                s << (p.filename + (p.enabled ? "\t1" : "\t0"));
            }
            return s;
        };
        QStringList before = snapshot();
        m_model->reconcile(available);
        if (snapshot() != before)
            profile->save(); // persist the reconciled plugin list only if it changed
    }
}

void PluginListView::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    // Resolve the row under the cursor, mapping through the proxy when the view is
    // sorted by a non-priority column, so per-plugin actions hit the right plugin.
    const QModelIndex idx = indexAt(viewport()->mapFromGlobal(event->globalPos()));
    int srcRow = -1;
    if (idx.isValid())
        srcRow = (model() == m_proxy) ? m_proxy->mapToSource(idx).row() : idx.row();

    if (srcRow >= 0) {
        const QString fn = pluginFilenameAt(idx);
        if (!fn.isEmpty()) {
            menu.addAction("Go to Origin Mod", [this, fn]{ emit pluginActivated(fn); });
            menu.addSeparator();
        }
    }
    menu.addAction("Enable All",  [this]{ setAllEnabled(true); });
    menu.addAction("Disable All", [this]{ setAllEnabled(false); });
    // Pin/unpin only makes sense for movable (non-official) plugins.
    if (srcRow >= 0 && !m_model->isRowOfficial(srcRow)) {
        const bool pinned = m_model->isRowPinned(srcRow);
        menu.addSeparator();
        menu.addAction(pinned ? "Unpin from Current Position" : "Pin to Current Position",
                       [this, srcRow]{ m_model->togglePin(srcRow); });
    }
    menu.exec(event->globalPos());
}

void PluginListView::setAllEnabled(bool enabled) {
    m_model->setAllEnabled(enabled);
}

void PluginListView::selectPlugin(const QString& filename) {
    Profile* profile = m_model->profile();
    if (filename.isEmpty() || !profile) return;
    // Find the source row by case-insensitive filename match against the live
    // plugin list (the model's DisplayRole may carry a pin-glyph prefix).
    const PluginList& pl = profile->pluginList();
    int srcRow = -1;
    for (int r = 0; r < pl.count(); ++r)
        if (pl.at(r).filename.compare(filename, Qt::CaseInsensitive) == 0) {
            srcRow = r; break;
        }
    if (srcRow < 0) return;
    QModelIndex idx = m_model->index(srcRow, PluginListModel::ColName);
    if (model() == m_proxy) idx = m_proxy->mapFromSource(idx);
    if (!idx.isValid()) return;
    selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    scrollTo(idx, QAbstractItemView::PositionAtCenter);
    setFocus();
}

void PluginListView::highlightPlugins(const QStringList& filenames) {
    QSet<QString> set;
    for (const QString& f : filenames) set.insert(f.toLower());
    m_model->setHighlighted(set);
}

QString PluginListView::pluginFilenameAt(const QModelIndex& viewIdx) const {
    Profile* profile = m_model->profile();
    if (!profile) return {};
    const QModelIndex idx = viewIdx.isValid() ? viewIdx : currentIndex();
    if (!idx.isValid()) return {};
    const int srcRow = (model() == m_proxy) ? m_proxy->mapToSource(idx).row() : idx.row();
    if (srcRow < 0 || srcRow >= profile->pluginList().count()) return {};
    return profile->pluginList().at(srcRow).filename; // already pin-glyph-free
}

void PluginListView::selectionChanged(const QItemSelection& selected,
                                      const QItemSelection& deselected) {
    QTableView::selectionChanged(selected, deselected);
    if (!selectionModel() || !selectionModel()->hasSelection()) return;
    const QString fn = pluginFilenameAt(QModelIndex());
    if (!fn.isEmpty()) emit pluginClicked(fn);
}

void PluginListView::mouseDoubleClickEvent(QMouseEvent* event) {
    const QString fn = pluginFilenameAt(indexAt(event->pos()));
    if (!fn.isEmpty()) { emit pluginActivated(fn); return; }
    QTableView::mouseDoubleClickEvent(event);
}
}
