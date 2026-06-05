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
    // Enable or disable every plugin at once (save + refresh).
    void setAllEnabled(bool enabled);

    int rowCount(const QModelIndex& = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    QVariant data(const QModelIndex&, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex&, const QVariant&, int role = Qt::EditRole) override;
    QVariant headerData(int, Qt::Orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex&) const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData*, Qt::DropAction, int, int, const QModelIndex&) const override;
    bool dropMimeData(const QMimeData*, Qt::DropAction, int row, int col, const QModelIndex& parent) override;

private:
    Profile* m_profile = nullptr;
    QSet<QString> m_highlight;
};

} // namespace solero
