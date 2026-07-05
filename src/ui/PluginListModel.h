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
    Profile* profile() const { return m_profile; }
    void reconcile(const QStringList& available);
    void setHighlighted(const QSet<QString>& lowerFilenames);
    // Enable or disable every plugin at once (save + refresh). Official plugins
    // are skipped (they can't be disabled).
    void setAllEnabled(bool enabled);
    // Enable or disable the plugins at the given source rows (save + refresh).
    // Official plugins in the set are skipped (they can't be disabled).
    void setEnabledForRows(const QList<int>& rows, bool enabled);

    // Toggle the pin on the plugin at `row` (records its current index when
    // pinning), persist, and refresh that row's pin indicator.
    void togglePin(int row);
    bool isRowPinned(int row) const;
    bool isRowOfficial(int row) const;

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

signals:
    // Emitted after a SUCCESSFUL manual reorder so the load order can be marked
    // dirty (LOOT may need to re-sort).
    void loadOrderChanged();
    // Emitted when a plugin's enabled state is toggled - lets the health
    // indicator / Problems panel recompute live (without marking the order dirty).
    void pluginEnabledChanged();

private:
    // Master files declared by `p` that are absent from the current plugin list.
    QStringList missingMasters(const PluginEntry& p) const;

    Profile* m_profile = nullptr;
    QSet<QString> m_highlight;
};

} // namespace solero
