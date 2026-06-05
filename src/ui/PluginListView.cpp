#include "PluginListView.h"
#include "PluginListModel.h"
#include "install/PluginScanner.h"
#include "core/AppConfig.h"
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QSet>
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
}

void PluginListView::applyHeaderLayout() {
    horizontalHeader()->setSectionResizeMode(PluginListModel::ColName, QHeaderView::Stretch);
    horizontalHeader()->resizeSection(PluginListModel::ColEnabled, 28);
    horizontalHeader()->resizeSection(PluginListModel::ColPriority, 40);
    horizontalHeader()->resizeSection(PluginListModel::ColFlags, 50);
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
void PluginListView::setProfile(Profile* profile) { m_model->setProfile(profile); }

void PluginListView::reconcileWith(Profile* profile, const QString& stagingRoot) {
    Q_UNUSED(stagingRoot);
    m_model->setProfile(profile);
    if (profile) {
        auto available = PluginScanner::scanGameData(AppConfig::instance().gameDir());
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

void PluginListView::highlightPlugins(const QStringList& filenames) {
    QSet<QString> set;
    for (const QString& f : filenames) set.insert(f.toLower());
    m_model->setHighlighted(set);
}
}
