#include "ModListModel.h"
#include <QColor>
#include <QFont>
#include <QDir>
#include <QDirIterator>
#include <QMimeData>
#include <QByteArray>
#include "core/AppConfig.h"
#include "ui/IconUtil.h"

namespace solero {

ModListModel::ModListModel(QObject* parent) : QAbstractItemModel(parent) {}

void ModListModel::setProfile(Profile* profile) {
    beginResetModel();
    m_profile = profile;
    // A new profile means entirely different staging contents - drop all caches.
    m_emptyCache.clear();
    m_overwriteHasFiles = -1;
    m_updates.clear();
    rebuildVisibleRows();
    endResetModel();
}

void ModListModel::setUpdateInfo(const QHash<QString, QPair<QString,QString>>& info) {
    m_updates = info;
    if (rowCount() > 0)
        emit dataChanged(index(0,0), index(rowCount()-1, ColCount-1));
}

void ModListModel::invalidateModCache(const QString& id) {
    if (id.isEmpty()) {
        m_emptyCache.clear();
        m_overwriteHasFiles = -1;
    } else {
        m_emptyCache.remove(id);
    }
    // Next repaint reads fresh; affected rows aren't cheaply known here so we
    // rely on the imminent refresh that callers already trigger.
}

void ModListModel::rebuild() {
    beginResetModel();
    rebuildVisibleRows();
    endResetModel();
}

void ModListModel::setDependencyWarnings(const QHash<QString,QStringList>& w) {
    m_depWarnings = w;
    if (rowCount() > 0)
        emit dataChanged(index(0,0), index(rowCount()-1, ColCount-1));
}

bool ModListModel::isModEmpty(const QString& id) const {
    auto it = m_emptyCache.constFind(id);
    if (it != m_emptyCache.constEnd()) return it.value();
    QDirIterator di(AppConfig::instance().stagingDir() + "/" + id,
                    QDir::Files, QDirIterator::Subdirectories);
    bool empty = !di.hasNext();
    m_emptyCache.insert(id, empty);
    return empty;
}

void ModListModel::rebuildVisibleRows() {
    m_visibleRows.clear();
    // Keep m_emptyCache / m_overwriteHasFiles persistent: rebuild() is called on
    // enable/move/rename/collapse - none of which change staged files. Caches are
    // only dropped in setProfile() or via invalidateModCache().
    if (!m_profile) return;

    bool inCollapsed = false;
    QString curParentId;       // id of the current top-level parent mod, if any
    bool curParentCollapsed = false;
    for (int i = 0; i < m_profile->modList().count(); ++i) {
        const auto& e = m_profile->modList().at(i);
        if (e.type == EntryType::Separator) {
            inCollapsed = e.collapsed;
            curParentId.clear();
            m_visibleRows.append(i); // separators always visible
        } else if (!e.parentId.isEmpty()) {
            // Child mod: hidden inside a collapsed separator, or when its parent is
            // collapsed (matched by parentId == the tracked top-level parent's id).
            bool hiddenByParent = (e.parentId == curParentId && curParentCollapsed);
            if (!inCollapsed && !hiddenByParent)
                m_visibleRows.append(i);
        } else {
            // Top-level mod (possibly a group parent). Remember it so following
            // children can be hidden when it's collapsed.
            curParentId = e.id;
            curParentCollapsed = e.collapsed;
            if (!inCollapsed)
                m_visibleRows.append(i);
        }
    }
    m_visibleRows.append(-1); // Overwrite always at bottom
}

int ModListModel::rawIndexForRow(int visibleRow) const {
    if (visibleRow < 0 || visibleRow >= m_visibleRows.size()) return -2;
    return m_visibleRows.at(visibleRow);
}

int ModListModel::rawToVisible(int rawIndex) const {
    return m_visibleRows.indexOf(rawIndex);
}

const ModEntry* ModListModel::entryAt(int visibleRow) const {
    int raw = rawIndexForRow(visibleRow);
    if (raw == -1 || raw == -2) return nullptr; // Overwrite or invalid
    if (!m_profile) return nullptr;
    return &m_profile->modList().at(raw);
}

void ModListModel::toggleCollapse(int visibleRow) {
    int raw = rawIndexForRow(visibleRow);
    if (raw < 0 || !m_profile) return;
    const auto* entry = &m_profile->modList().at(raw);
    if (entry->type != EntryType::Separator) return;
    ModEntry updated = *entry;
    updated.collapsed = !updated.collapsed;
    m_profile->modList().update(entry->id, updated);
    m_profile->save();
    rebuild();
}

void ModListModel::toggleModCollapse(int visibleRow) {
    int raw = rawIndexForRow(visibleRow);
    if (raw < 0 || !m_profile) return;
    if (!isGroupParent(raw)) return;
    const auto* entry = &m_profile->modList().at(raw);
    ModEntry updated = *entry;
    updated.collapsed = !updated.collapsed;
    m_profile->modList().update(entry->id, updated);
    m_profile->save();
    rebuild();
}

bool ModListModel::isGroupParent(int raw) const {
    if (!m_profile) return false;
    const auto& list = m_profile->modList();
    if (raw < 0 || raw >= list.count()) return false;
    const auto& e = list.at(raw);
    if (e.type != EntryType::Mod) return false;
    int next = raw + 1;
    if (next >= list.count()) return false;
    const auto& n = list.at(next);
    return n.type == EntryType::Mod && n.parentId == e.id;
}

bool ModListModel::isGroupChild(int raw) const {
    if (!m_profile) return false;
    const auto& list = m_profile->modList();
    if (raw < 0 || raw >= list.count()) return false;
    const auto& e = list.at(raw);
    return e.type == EntryType::Mod && !e.parentId.isEmpty();
}

int ModListModel::groupChildCount(int parentRaw) const {
    if (!m_profile) return 0;
    const auto& list = m_profile->modList();
    if (parentRaw < 0 || parentRaw >= list.count()) return 0;
    const auto& parent = list.at(parentRaw);
    if (parent.type != EntryType::Mod) return 0;
    int count = 0;
    for (int i = parentRaw + 1; i < list.count(); ++i) {
        const auto& e = list.at(i);
        if (e.type == EntryType::Mod && e.parentId == parent.id) ++count;
        else break;
    }
    return count;
}

QModelIndex ModListModel::index(int row, int col, const QModelIndex&) const {
    return createIndex(row, col);
}

int ModListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !m_profile) return 0;
    return m_visibleRows.size();
}

QVariant ModListModel::data(const QModelIndex& idx, int role) const {
    if (!m_profile || !idx.isValid()) return {};

    if (role == Qt::TextAlignmentRole && idx.column() == ColPriority)
        return QVariant(Qt::AlignCenter);

    int raw = rawIndexForRow(idx.row());
    bool isOverwrite = (raw == -1);

    if (isOverwrite) {
        if (role == Qt::DisplayRole) {
            if (idx.column() == ColName)     return "[Overwrite]";
            if (idx.column() == ColPriority) return QVariant();
        }
        if (role == Qt::ForegroundRole || role == Qt::FontRole) {
            if (m_overwriteHasFiles < 0) {
                QString owDir = AppConfig::dataRoot() + "/overwrite";
                QDirIterator it(owDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
                m_overwriteHasFiles = it.hasNext() ? 1 : 0;
            }
            bool hasFiles = (m_overwriteHasFiles == 1);
            if (role == Qt::ForegroundRole)
                return hasFiles ? QColor(Qt::red) : QColor(Qt::darkYellow);
            QFont f; f.setBold(hasFiles); return f;
        }
        return {};
    }

    if (raw < 0) return {};
    const auto& entry = m_profile->modList().at(raw);
    bool isSep = (entry.type == EntryType::Separator);

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
            case ColPriority: {
                if (isSep) return QVariant();
                // Contiguous position among mod-type entries only (1-based),
                // ignoring separators and hidden mods so the column reads 1..N.
                int pos = 0;
                for (int i = 0; i < raw; ++i)
                    if (m_profile->modList().at(i).type == EntryType::Mod) ++pos;
                return pos + 1;
            }
            case ColName:
                if (isSep) {
                    QString arrow = entry.collapsed ? "\xe2\x96\xb6" : "\xe2\x96\xbc";
                    return QString("%1  %2").arg(arrow, entry.name);
                }
                if (isGroupParent(raw)) {
                    QString arrow = entry.collapsed ? "\xe2\x96\xb6 " : "\xe2\x96\xbc "; // ▶/▼
                    return arrow + entry.name;
                }
                if (isGroupChild(raw))
                    return QString("    \xe2\x94\x94 ") + entry.name; // 4 spaces + "└ "
                return entry.name;
            case ColVersion:
                if (isSep) return QVariant();
                if (m_updates.contains(entry.id)) {
                    const auto& u = m_updates.value(entry.id);
                    return u.first + " \xe2\x86\x92 " + u.second; // installed -> latest
                }
                return entry.version;
            case ColFlags: {
                if (isSep) return QString();
                QStringList parts;
                if (entry.isOutputMod) parts << "Output";
                if (entry.hasFomodChoices) parts << "FOMOD";
                if (m_updates.contains(entry.id)) parts << "\xe2\xac\x86 Update"; // ⬆ Update
                QString flags = parts.join(" ");
                if (m_depWarnings.contains(entry.id)) flags = "\xe2\x9a\xa0 " + flags; // ⚠
                return flags;
            }
            default: return {};
        }
    }
    if (role == Qt::ToolTipRole && !isSep && m_updates.contains(entry.id)) {
        const auto& u = m_updates.value(entry.id);
        return QString("Update available: %1 \xe2\x86\x92 %2").arg(u.first, u.second); // installed -> latest
    }
    if (role == Qt::ToolTipRole && !isSep && m_depWarnings.contains(entry.id))
        return m_depWarnings.value(entry.id).join("\n");
    if (role == Qt::CheckStateRole && idx.column() == ColEnabled && !isSep)
        return entry.enabled ? Qt::Checked : Qt::Unchecked;
    if (role == Qt::EditRole && idx.column() == ColName)
        return entry.name;
    if (role == Qt::DecorationRole && isSep && idx.column() == ColName && !entry.icon.isEmpty()) {
        return renderSvgIcon(entry.icon, solero::contrastText(QColor(entry.color)), 20);
    }
    // Out-of-date mod entries: a yellow up-arrow next to the name.
    if (role == Qt::DecorationRole && !isSep && idx.column() == ColName
            && entry.type == EntryType::Mod && m_updates.contains(entry.id)) {
        return solero::yellowUpArrowIcon();
    }
    if (role == Qt::FontRole && isSep) {
        QFont f; f.setBold(true); return f;
    }
    if (role == Qt::FontRole && !isSep && entry.type == EntryType::Mod && isModEmpty(entry.id)) {
        QFont f; f.setItalic(true); return f;
    }
    if (role == Qt::BackgroundRole && isSep && !entry.color.isEmpty())
        return QColor(entry.color);
    if (role == Qt::ForegroundRole && isSep && !entry.color.isEmpty())
        return solero::contrastText(QColor(entry.color));
    // Out-of-date mods: tint the Version cell orange so they stand out.
    if (role == Qt::ForegroundRole && !isSep && idx.column() == ColVersion
            && m_updates.contains(entry.id))
        return QColor("#e67e22");
    if (role == Qt::ForegroundRole && !isSep && entry.isOutputMod)
        return QColor("#7f9cc4");
    if (role == Qt::UserRole)
        return entry.id;

    return {};
}

bool ModListModel::setData(const QModelIndex& idx, const QVariant& value, int role) {
    if (!m_profile) return false;
    int raw = rawIndexForRow(idx.row());
    if (raw < 0) return false;
    if (role == Qt::CheckStateRole && idx.column() == ColEnabled) {
        m_profile->modList().setEnabled(
            m_profile->modList().at(raw).id,
            value.toInt() == Qt::Checked);
        m_profile->save();
        emit dataChanged(idx, idx, {role});
        emit modsChanged();
        return true;
    }
    if (role == Qt::EditRole && idx.column() == ColName) {
        const auto& cur = m_profile->modList().at(raw);
        QString nn = value.toString().trimmed();
        if (nn.isEmpty()) return false;
        ModEntry e = cur;
        e.name = nn;
        m_profile->modList().update(e.id, e);
        m_profile->save();
        emit dataChanged(idx, idx);
        return true;
    }
    return false;
}

QVariant ModListModel::headerData(int section, Qt::Orientation, int role) const {
    if (role != Qt::DisplayRole) return {};
    switch (section) {
        case ColEnabled:  return "";
        case ColPriority: return "#";
        case ColName:     return "Name";
        case ColVersion:  return "Version";
        case ColFlags:    return "Flags";
        default: return {};
    }
}

Qt::ItemFlags ModListModel::flags(const QModelIndex& idx) const {
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                      Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    int raw = rawIndexForRow(idx.row());
    if (raw >= 0 && m_profile) {
        const auto& entry = m_profile->modList().at(raw);
        // Group children move only with their parent - withhold independent drag.
        if (isGroupChild(raw))
            f &= ~Qt::ItemIsDragEnabled;
        if (entry.type == EntryType::Mod && idx.column() == ColEnabled)
            f |= Qt::ItemIsUserCheckable;
        if (entry.type == EntryType::Mod && idx.column() == ColName)
            f |= Qt::ItemIsEditable;
        if (entry.type == EntryType::Separator && idx.column() == ColName)
            f |= Qt::ItemIsEditable;
    }
    return f;
}

Qt::DropActions ModListModel::supportedDropActions() const {
    return Qt::MoveAction;
}

bool ModListModel::moveRows(const QModelIndex&, int src, int count, const QModelIndex&, int dst) {
    if (!m_profile) return false;
    int srcRaw = rawIndexForRow(src);
    if (srcRaw < 0) return false; // can't move the Overwrite row (-1) or an invalid one

    auto& list = m_profile->modList();

    // Destination raw index. The Overwrite row (visible) maps to raw -1; treat a
    // drop there as "append to the end of the real mod list" (raw == count), i.e.
    // just above the pinned Overwrite. Any other invalid dst aborts.
    int dstRaw = rawIndexForRow(dst);
    if (dstRaw == -1) dstRaw = list.count();
    else if (dstRaw < 0) return false;

    const bool draggingSeparator =
        list.at(srcRaw).type == EntryType::Separator;

    // Dragging a group parent moves the parent + its contiguous child block as a
    // unit. The destination is snapped to a group boundary so the block never
    // lands between some other parent and its children.
    if (!draggingSeparator && isGroupParent(srcRaw)) {
        int blockLen = 1 + groupChildCount(srcRaw);
        // Snap dstRaw to a group boundary: if it would land on a child (i.e. just
        // after some parent, splitting that group), push it past the last child.
        if (dstRaw > 0 && dstRaw < list.count()) {
            const auto& at = list.at(dstRaw);
            if (at.type == EntryType::Mod && !at.parentId.isEmpty()) {
                // dstRaw points at a child: advance to the end of that child run.
                while (dstRaw < list.count()
                       && list.at(dstRaw).type == EntryType::Mod
                       && !list.at(dstRaw).parentId.isEmpty())
                    ++dstRaw;
            }
        }
        int destRaw = dstRaw;
        if (destRaw > srcRaw) destRaw -= blockLen;
        if (destRaw < 0) destRaw = 0;

        if (destRaw == srcRaw) return false; // no-op

        beginResetModel();
        list.moveSection(srcRaw, blockLen, destRaw);
        m_profile->save();
        rebuildVisibleRows();
        endResetModel();
        emit modsChanged();
        return true;
    }

    if (draggingSeparator) {
        // Move the whole section: the separator plus every entry after it up to
        // (but excluding) the next separator or the end of the list. Reordering a
        // section changes deploy order, so we reset + persist + emit modsChanged.
        int blockLen = 1;
        for (int i = srcRaw + 1; i < list.count(); ++i) {
            if (list.at(i).type == EntryType::Separator) break;
            ++blockLen;
        }
        // Destination in raw-index terms, adjusted for the block's removal so the
        // section lands where the user dropped it.
        int destRaw = dstRaw;
        if (destRaw > srcRaw) destRaw -= blockLen;
        if (destRaw < 0) destRaw = 0;

        beginResetModel();
        list.moveSection(srcRaw, blockLen, destRaw);
        m_profile->save();
        rebuildVisibleRows();
        endResetModel();
        emit modsChanged();
        return true;
    }

    // QList::move() needs a valid destination index in [0, count-1]. A drop at the
    // end (dstRaw == count, i.e. just above Overwrite) maps to the last raw slot.
    int moveTo = dstRaw;
    if (moveTo >= list.count()) moveTo = list.count() - 1;
    if (moveTo == srcRaw) return false; // no-op

    beginMoveRows({}, src, src, {}, dst > src ? dst + 1 : dst);
    list.move(srcRaw, moveTo);
    m_profile->save();
    rebuildVisibleRows();
    endMoveRows();
    emit modsChanged();
    return true;
}

static const char* kModMime = "application/x-solero-mod-row";

QStringList ModListModel::mimeTypes() const { return { QString::fromLatin1(kModMime) }; }

QMimeData* ModListModel::mimeData(const QModelIndexList& indexes) const {
    auto* mime = new QMimeData;
    int row = -1;
    for (const auto& idx : indexes) { if (idx.isValid()) { row = idx.row(); break; } }
    mime->setData(QString::fromLatin1(kModMime), QByteArray::number(row));
    return mime;
}

bool ModListModel::canDropMimeData(const QMimeData* data, Qt::DropAction,
                                   int, int, const QModelIndex&) const {
    if (!m_profile || !data->hasFormat(QString::fromLatin1(kModMime))) return false;
    return true;
}

bool ModListModel::dropMimeData(const QMimeData* data, Qt::DropAction,
                                int row, int, const QModelIndex& parent) {
    if (!m_profile || !data->hasFormat(QString::fromLatin1(kModMime))) return false;
    const int srcVisible = data->data(QString::fromLatin1(kModMime)).toInt();

    // Never drag the pinned Overwrite row.
    if (rawIndexForRow(srcVisible) == -1) return false;

    // Destination VISIBLE insertion row.
    int dstVisible;
    if (row >= 0)            dstVisible = row;
    else if (parent.isValid()) dstVisible = parent.row();
    else                     dstVisible = rowCount();

    // Clamp so nothing lands below the pinned Overwrite. rowCount()-1 is the
    // Overwrite's visible index, which moveRows treats as "end of the mod list".
    if (dstVisible < 0) dstVisible = 0;
    if (dstVisible > rowCount() - 1) dstVisible = rowCount() - 1;

    // moveRows performs the move, persists, refreshes, and emits modsChanged().
    // Return false either way so the view doesn't additionally removeRows.
    moveRows({}, srcVisible, 1, {}, dstVisible);
    return false;
}

} // namespace solero
