#include "PluginListView.h"
#include "PluginListModel.h"
#include "install/PluginScanner.h"
#include <QHeaderView>
namespace solero {
PluginListView::PluginListView(QWidget* parent) : QTableView(parent) {
    m_model = new PluginListModel(this);
    setModel(m_model);
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    horizontalHeader()->setSectionResizeMode(PluginListModel::ColName, QHeaderView::Stretch);
    horizontalHeader()->resizeSection(PluginListModel::ColEnabled, 28);
    horizontalHeader()->resizeSection(PluginListModel::ColPriority, 40);
    horizontalHeader()->resizeSection(PluginListModel::ColFlags, 50);
    verticalHeader()->hide();
}
void PluginListView::setProfile(Profile* profile) { m_model->setProfile(profile); }

void PluginListView::reconcileWith(Profile* profile, const QString& stagingRoot) {
    m_model->setProfile(profile);
    if (profile) {
        auto available = PluginScanner::scan(profile->modList(), stagingRoot);
        m_model->reconcile(available);
        profile->save(); // persist the reconciled plugin list
    }
}
}
