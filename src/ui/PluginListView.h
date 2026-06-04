#pragma once
#include <QTableView>
#include "core/Profile.h"
class QSortFilterProxyModel;
namespace solero {
class PluginListModel;
class PluginListView : public QTableView {
    Q_OBJECT
public:
    explicit PluginListView(QWidget* parent = nullptr);
    void setProfile(Profile* profile);
    void reconcileWith(Profile* profile, const QString& stagingRoot);
    void highlightPlugins(const QStringList& filenames);
private slots:
    void onSortChanged(int col, Qt::SortOrder order);
private:
    void applyHeaderLayout();
    PluginListModel* m_model;
    QSortFilterProxyModel* m_proxy;
};
}
