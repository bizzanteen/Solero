#pragma once
#include <QTableView>
#include "core/Profile.h"
namespace solero {
class PluginListModel;
class PluginListView : public QTableView {
    Q_OBJECT
public:
    explicit PluginListView(QWidget* parent = nullptr);
    void setProfile(Profile* profile);
private:
    PluginListModel* m_model;
};
}
