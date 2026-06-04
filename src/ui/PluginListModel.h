#pragma once
#include <QAbstractTableModel>
#include <QSet>
#include <QColor>
#include "core/Profile.h"

namespace solero {

class PluginListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColEnabled = 0, ColPriority, ColName, ColFlags, ColCount };
    explicit PluginListModel(QObject* parent = nullptr);
    void setProfile(Profile* profile);
    void reconcile(const QStringList& available);
    void setHighlighted(const QSet<QString>& lowerFilenames);

    int rowCount(const QModelIndex& = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    QVariant data(const QModelIndex&, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex&, const QVariant&, int role = Qt::EditRole) override;
    QVariant headerData(int, Qt::Orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex&) const override;
    Qt::DropActions supportedDropActions() const override;
    bool moveRows(const QModelIndex&, int src, int count, const QModelIndex&, int dst) override;

private:
    Profile* m_profile = nullptr;
    QSet<QString> m_highlight;
};

} // namespace solero
