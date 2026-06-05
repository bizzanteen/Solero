#pragma once
#include <QAbstractItemModel>
#include <QHash>
#include <QStringList>
#include "core/Profile.h"

namespace solero {

class ModListModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Column { ColEnabled = 0, ColPriority, ColName, ColVersion, ColFlags, ColCount };

    explicit ModListModel(QObject* parent = nullptr);
    void setProfile(Profile* profile);
    Profile* profile() const { return m_profile; }

    QModelIndex index(int row, int col, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex&) const override { return {}; }
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    Qt::DropActions supportedDropActions() const override;
    bool moveRows(const QModelIndex&, int src, int count, const QModelIndex&, int dst) override;

    // Map from visible row index to raw ModList index (-1 = Overwrite)
    int rawIndexForRow(int visibleRow) const;
    int rawToVisible(int rawIndex) const;
    const ModEntry* entryAt(int visibleRow) const;
    void toggleCollapse(int visibleRow);
    void rebuild();  // call after any structural change
    void setDependencyWarnings(const QHash<QString,QStringList>& w);

    // Invalidate cached disk scans (empty-mod / Overwrite "has files"). Call only
    // when a mod's staged files actually change. Empty id clears the whole cache
    // (and the Overwrite cache); a specific id removes just that entry.
    void invalidateModCache(const QString& id = QString());

signals:
    void modsChanged();

private:
    Profile* m_profile = nullptr;
    QList<int> m_visibleRows; // raw indices into ModList, -1 = Overwrite
    QHash<QString,QStringList> m_depWarnings;
    mutable QHash<QString,bool> m_emptyCache;
    mutable int m_overwriteHasFiles = -1; // tri-state cache: -1 unknown, 0 no, 1 yes

    void rebuildVisibleRows();
    bool isModEmpty(const QString& id) const;
};

} // namespace solero
