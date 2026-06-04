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
    setDragDropMode(QAbstractItemView::InternalMove);
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
        m_model->reconcile(available);
        profile->save(); // persist the reconciled plugin list
    }
}

void PluginListView::highlightPlugins(const QStringList& filenames) {
    QSet<QString> set;
    for (const QString& f : filenames) set.insert(f.toLower());
    m_model->setHighlighted(set);
}
}
