#include "PluginListView.h"
#include "PluginListModel.h"
#include "install/PluginScanner.h"
#include "core/AppConfig.h"
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QSet>
#include <QMenu>
#include <QContextMenuEvent>
#include <QShowEvent>
#include <QTimer>
namespace solero {
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
    applyHeaderLayout();
    verticalHeader()->hide();
    connect(horizontalHeader(), &QHeaderView::sortIndicatorChanged,
            this, &PluginListView::onSortChanged);
    connect(m_model, &PluginListModel::loadOrderChanged,
            this, &PluginListView::loadOrderChanged);
    // Resizing another column lets the Plugin column absorb the slack (no gap).
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this](int idx, int, int) {
        if (idx != PluginListModel::ColName) fillNameColumn();
    });
}

void PluginListView::applyHeaderLayout() {
    // All columns Interactive (no middle Stretch) so manual resizes behave
    // predictably; the Plugin column is auto-fitted on first show instead.
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader()->resizeSection(PluginListModel::ColEnabled, 28);
    horizontalHeader()->resizeSection(PluginListModel::ColPriority, 40);
    horizontalHeader()->resizeSection(PluginListModel::ColFlags, 50);
}

void PluginListView::autoSizeColumns() {
    horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
    fillNameColumn();
}

// Resize the Plugin column so the columns always span the full viewport.
void PluginListView::fillNameColumn() {
    if (!model()) return;
    const int vw = viewport()->width();
    if (vw <= 0) return;
    int other = 0;
    for (int c = 0; c < model()->columnCount(); ++c)
        if (c != PluginListModel::ColName) other += horizontalHeader()->sectionSize(c);
    const int target = qMax(160, vw - other);
    if (target != horizontalHeader()->sectionSize(PluginListModel::ColName))
        horizontalHeader()->resizeSection(PluginListModel::ColName, target);
}

void PluginListView::resizeEvent(QResizeEvent* event) {
    QTableView::resizeEvent(event);
    fillNameColumn();
}

void PluginListView::showEvent(QShowEvent* event) {
    QTableView::showEvent(event);
    if (!m_didAutoSize) {
        m_didAutoSize = true;
        QTimer::singleShot(0, this, [this]{ autoSizeColumns(); });
    }
}

void PluginListView::onSortChanged(int col, Qt::SortOrder order) {
    if (col == PluginListModel::ColPriority) {
        if (model() != m_model) { setModel(m_model); applyHeaderLayout(); }
        setDragDropMode(QAbstractItemView::InternalMove);
        setDragEnabled(true); setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropOverwriteMode(false);
        setDefaultDropAction(Qt::MoveAction);
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
    // Re-fit columns once the profile's plugins first populate. The viewport-width
    // guard in autoSizeColumns() makes this a no-op if the view isn't shown yet.
    QTimer::singleShot(0, this, [this]{ autoSizeColumns(); });
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
    menu.addAction("Enable all",  [this]{ setAllEnabled(true); });
    menu.addAction("Disable all", [this]{ setAllEnabled(false); });
    menu.exec(event->globalPos());
}

void PluginListView::setAllEnabled(bool enabled) {
    m_model->setAllEnabled(enabled);
}

void PluginListView::highlightPlugins(const QStringList& filenames) {
    QSet<QString> set;
    for (const QString& f : filenames) set.insert(f.toLower());
    m_model->setHighlighted(set);
}
}
