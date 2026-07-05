#include "PluginListView.h"
#include "PluginListModel.h"
#include "LootRulesEditor.h"
#include "install/PluginScanner.h"
#include "install/PluginFlagEditor.h"
#include "core/AppConfig.h"
#include "core/ModList.h"
#include "core/StagingFolder.h"
#include "core/Log.h"
#include <QHeaderView>
#include <QDir>
#include <QInputDialog>
#include <QSortFilterProxyModel>
#include <QItemSelectionModel>
#include <QSet>
#include <QMenu>
#include <QMessageBox>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QShowEvent>
#include <QTimer>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QStyle>
#include <QPainter>
#include <QPaintEvent>
#include <QToolTip>
#include <QCursor>
#include <QIcon>
#include "ElideDelegate.h"
namespace solero {

namespace {
// The model exposes a warning via Qt::DecorationRole on the Plugin column - the
// dirty-plugin (ITM/UDR) flag. The default delegate draws that icon to
// the LEFT of the name; instead, suppress the leading icon and paint it
// immediately after the text so the warning trails the plugin name. The model's
// tooltip (the cleaning reason) is left untouched, so hovering still works.
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
    // Draw the dirty-plugin warning icon after the plugin name (see NameDelegate).
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

void PluginListView::paintEvent(QPaintEvent* event) {
    QTableView::paintEvent(event);
    // Mirror the mod list's empty-state overlay (ModListView): when no plugins are
    // present, draw a centered hint over the viewport instead of a blank pane.
    if (model() && model()->rowCount() == 0) {
        QPainter painter(viewport());
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        const QRect r = viewport()->rect().adjusted(40, 40, -40, -40);
        painter.drawText(r, Qt::AlignCenter | Qt::TextWordWrap,
            QStringLiteral("No plugins detected ") + QChar('-')
                + QStringLiteral(" install mods or check your Data path."));
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
        // Sorting by a non-priority column silently disabled drag-reorder; surface a
        // transient hint (near the header) so the lost capability isn't a mystery.
        if (!m_filterActive)
            QToolTip::showText(
                horizontalHeader()->mapToGlobal(QPoint(0, horizontalHeader()->height())),
                QStringLiteral("Reordering is only available when sorted by priority "
                               "(#). Click the # column to restore drag-reorder."),
                this);
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
        QSet<QString> availableLower; // O(1) case-insensitive presence test
        for (const QString& p : available) availableLower.insert(p.toLower());
        for (const QString& p : staged)
            if (!availableLower.contains(p.toLower())) {
                available << p;
                availableLower.insert(p.toLower());
            }
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
        // Mirror the mod list: if the right-clicked row isn't in the selection,
        // make it the sole selection so per-selection actions act on it.
        if (!selectionModel()->isRowSelected(idx.row(), idx.parent()))
            selectionModel()->select(
                idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    // Per-selection enable/disable (skips locked/official plugins in the model).
    const QList<int> selRows = selectedSourceRows();
    if (!selRows.isEmpty()) {
        menu.addAction("Enable Selected",
                       [this, selRows]{ m_model->setEnabledForRows(selRows, true); });
        menu.addAction("Disable Selected",
                       [this, selRows]{ m_model->setEnabledForRows(selRows, false); });
        menu.addSeparator();
    }
    menu.addAction("Enable All",  [this]{ setAllEnabled(true); });
    menu.addAction("Disable All", [this]{ setAllEnabled(false); });

    // (ESL flag) + (LOOT rules): per-plugin actions on the row under
    // the cursor. Both operate on staged, non-official plugins.
    if (srcRow >= 0 && !m_model->isRowOfficial(srcRow)) {
        const QString fn = pluginFilenameAt(idx);
        if (!fn.isEmpty()) {
            menu.setToolTipsVisible(true); // so a disabled action can explain itself

            // : ESL / light-master flag
            const QString staged = stagedPluginPath(fn);
            if (!staged.isEmpty() && PluginFlagEditor::isTes4(staged)) {
                bool ok = false;
                const bool light = PluginFlagEditor::isLight(staged, &ok);
                if (ok) {
                    menu.addSeparator();
                    if (light) {
                        menu.addAction("Remove ESL Flag",
                                       [this, fn]{ applyEslFlag(fn, false); });
                    } else {
                        const EslEligibility elig = PluginFlagEditor::checkEslEligible(staged);
                        QAction* a = menu.addAction("Add ESL Flag",
                                       [this, fn]{ applyEslFlag(fn, true); });
                        if (!elig.eligible) {
                            a->setEnabled(false);
                            a->setToolTip(elig.reason);
                        }
                    }
                }
            }

            // : per-plugin LOOT rules
            menu.addSeparator();
            QMenu* loot = menu.addMenu("LOOT Rule");
            using LR = LootRulesEditor::LootRule;
            loot->addAction("Load After" + QString(QChar(0x2026)),
                            [this, fn]{ addLootRuleFor(fn, int(LR::LoadAfter)); });
            loot->addAction("Requires" + QString(QChar(0x2026)),
                            [this, fn]{ addLootRuleFor(fn, int(LR::Requires)); });
            loot->addAction("Incompatible With" + QString(QChar(0x2026)),
                            [this, fn]{ addLootRuleFor(fn, int(LR::Incompatible)); });
            loot->addAction("Set LOOT Group" + QString(QChar(0x2026)),
                            [this, fn]{ addLootRuleFor(fn, int(LR::SetGroup)); });
        }
    }

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
    // Whole-list toggles have no undo, so confirm on large lists before wiping
    // every plugin's enabled state.
    const int n = m_model->rowCount();
    if (n > 20) {
        const auto btn = QMessageBox::question(
            this,
            enabled ? "Enable All Plugins" : "Disable All Plugins",
            QStringLiteral("%1 all %2 plugins?")
                .arg(enabled ? "Enable" : "Disable").arg(n),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes) return;
    }
    m_model->setAllEnabled(enabled);
}

QString PluginListView::stagedPluginPath(const QString& filename) const {
    Profile* profile = m_model->profile();
    if (!profile || filename.isEmpty()) return {};
    const QString staging = AppConfig::instance().stagingDir();
    // Mod-list order is low->high priority, so the last enabled provider wins the
    // deploy conflict - that's the file the user sees, and the one we must edit.
    QString winner;
    for (const auto& e : profile->modList()) {
        if (e.type != EntryType::Mod || !e.enabled) continue;
        const QString data = stagingPathFor(staging, e) + "/Data";
        QDir d(data);
        if (!d.exists()) continue;
        const auto plugins = d.entryList({"*.esp","*.esm","*.esl","*.ESP","*.ESM","*.ESL"},
                                         QDir::Files);
        for (const QString& f : plugins)
            if (f.compare(filename, Qt::CaseInsensitive) == 0) { winner = data + "/" + f; break; }
    }
    return winner;
}

void PluginListView::applyEslFlag(const QString& filename, bool set) {
    const QString staged = stagedPluginPath(filename);
    if (staged.isEmpty()) {
        QMessageBox::warning(this, "ESL Flag",
            "Could not locate the staged plugin file to edit.");
        return;
    }
    QString err;
    if (!PluginFlagEditor::setLightFlag(staged, set, &err)) {
        QMessageBox::warning(this, "ESL Flag",
            err.isEmpty() ? QStringLiteral("Failed to update the ESL flag.") : err);
        return;
    }
    qCInfo(lcInstall) << "PluginListView:" << (set ? "added" : "removed")
                      << "ESL flag on" << filename;
    m_model->setPluginLight(filename, set); // refresh the row's Type/flags immediately
}

QStringList PluginListView::otherPluginNames(const QString& self) const {
    QStringList out;
    Profile* profile = m_model->profile();
    if (!profile) return out;
    const PluginList& pl = profile->pluginList();
    for (int i = 0; i < pl.count(); ++i) {
        const QString& fn = pl.at(i).filename;
        if (fn.compare(self, Qt::CaseInsensitive) != 0) out << fn;
    }
    return out;
}

void PluginListView::addLootRuleFor(const QString& filename, int rule) {
    Profile* profile = m_model->profile();
    if (!profile) return;
    using LR = LootRulesEditor::LootRule;
    const LR kind = static_cast<LR>(rule);

    QString target;
    bool okDlg = false;
    if (kind == LR::SetGroup) {
        target = QInputDialog::getText(this, "Set LOOT Group",
            QStringLiteral("Group name for %1:").arg(filename),
            QLineEdit::Normal, QString(), &okDlg).trimmed();
    } else {
        const QStringList others = otherPluginNames(filename);
        if (others.isEmpty()) {
            QMessageBox::information(this, "LOOT Rule",
                "There are no other plugins to reference.");
            return;
        }
        const QString title = kind == LR::LoadAfter ? "Load After"
                            : kind == LR::Requires  ? "Requires"
                            :                         "Incompatible With";
        target = QInputDialog::getItem(this, title,
            QStringLiteral("%1 %2:").arg(filename,
                kind == LR::LoadAfter ? "loads after"
              : kind == LR::Requires  ? "requires"
              :                         "is incompatible with"),
            others, 0, false, &okDlg).trimmed();
    }
    if (!okDlg || target.isEmpty()) return;

    QString err;
    if (!LootRulesEditor::addPluginRule(profile, filename, kind, target, &err)) {
        QMessageBox::warning(this, "LOOT Rule",
            err.isEmpty() ? QStringLiteral("Could not save the LOOT rule.") : err);
        return;
    }
    qCInfo(lcLoot) << "PluginListView: added LOOT rule for" << filename << "->" << target;
}

QList<int> PluginListView::selectedSourceRows() const {
    QList<int> rows;
    const auto sel = selectionModel() ? selectionModel()->selectedRows()
                                      : QModelIndexList{};
    for (const QModelIndex& idx : sel) {
        const int r = (model() == m_proxy) ? m_proxy->mapToSource(idx).row()
                                           : idx.row();
        if (r >= 0) rows << r;
    }
    return rows;
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
