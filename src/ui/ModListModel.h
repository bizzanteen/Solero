#pragma once
#include <QAbstractItemModel>
#include <QHash>
#include <QPair>
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
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData*, Qt::DropAction, int row, int col, const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData*, Qt::DropAction, int row, int col, const QModelIndex& parent) override;

    // Map from visible row index to raw ModList index (-1 = Overwrite)
    int rawIndexForRow(int visibleRow) const;
    int rawToVisible(int rawIndex) const;
    const ModEntry* entryAt(int visibleRow) const;
    void toggleCollapse(int visibleRow);
    // Toggle the collapsed state of a group-PARENT mod (mirrors toggleCollapse,
    // which is for separators). No-op if the entry isn't a group parent.
    void toggleModCollapse(int visibleRow);

    // Multi-file group helpers (operate on raw ModList indices).
    // A parent is a Mod immediately followed by ≥1 Mod whose parentId == its id.
    bool isGroupParent(int raw) const;
    // A child is a Mod with a non-empty parentId.
    bool isGroupChild(int raw) const;
    // Count of the contiguous run of child Mods after a parent.
    int groupChildCount(int parentRaw) const;
    void rebuild();  // call after any structural change
    void setDependencyWarnings(const QHash<QString,QStringList>& w);
    // Mark mods that have a newer version available. Key = mod id; value =
    // {installedVersion, latestVersion}. Only includes mods with an update.
    void setUpdateInfo(const QHash<QString, QPair<QString,QString>>& info);
    // MO2-style conflict highlight for the selected mod: id -> 1 (green: this mod
    // overwrites the selection) / 2 (red: overwritten by the selection). Empty = clear.
    void setConflictHighlights(const QHash<QString,int>& roles);

    // Invalidate cached disk scans (empty-mod / Overwrite "has files"). Call only
    // when a mod's staged files actually change. Empty id clears the whole cache
    // (and the Overwrite cache); a specific id removes just that entry.
    void invalidateModCache(const QString& id = QString());

signals:
    void modsChanged();

private:
    Profile* m_profile = nullptr;
    QList<int> m_visibleRows; // raw indices into ModList, -1 = Overwrite
    // Precomputed Priority column: raw index of a Mod -> its 1-based contiguous
    // position among Mod-type entries (children included), in raw order. Rebuilt
    // in rebuildVisibleRows() so the Priority cell is O(1) instead of O(n).
    QHash<int,int> m_priorityByRaw;
    QHash<QString,QStringList> m_depWarnings;
    QHash<QString, QPair<QString,QString>> m_updates; // modId -> {installed, latest}
    QHash<QString,int> m_conflictHi; // modId -> 1 green / 2 red (selection conflicts)
    mutable QHash<QString,bool> m_emptyCache;
    mutable int m_overwriteHasFiles = -1; // tri-state cache: -1 unknown, 0 no, 1 yes

    void rebuildVisibleRows();
    bool isModEmpty(const QString& id) const;
};

} // namespace solero
