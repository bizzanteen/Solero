#include "ModListModel.h"
#include <QColor>
#include <QFont>
#include <QDir>
#include <QDirIterator>
#include "core/AppConfig.h"
#include "ui/IconUtil.h"

namespace solero {

ModListModel::ModListModel(QObject* parent) : QAbstractItemModel(parent) {}

void ModListModel::setProfile(Profile* profile) {
    beginResetModel();
    m_profile = profile;
    rebuildVisibleRows();
    endResetModel();
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
    m_emptyCache.clear();
    if (!m_profile) return;

    bool inCollapsed = false;
    for (int i = 0; i < m_profile->modList().count(); ++i) {
        const auto& e = m_profile->modList().at(i);
        if (e.type == EntryType::Separator) {
            inCollapsed = e.collapsed;
            m_visibleRows.append(i); // separators always visible
        } else if (!inCollapsed) {
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

QModelIndex ModListModel::index(int row, int col, const QModelIndex&) const {
    return createIndex(row, col);
}

int ModListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !m_profile) return 0;
    return m_visibleRows.size();
}

QVariant ModListModel::data(const QModelIndex& idx, int role) const {
    if (!m_profile || !idx.isValid()) return {};

    int raw = rawIndexForRow(idx.row());
    bool isOverwrite = (raw == -1);

    if (isOverwrite) {
        if (role == Qt::DisplayRole) {
            if (idx.column() == ColName)     return "[Overwrite]";
            if (idx.column() == ColPriority) return QVariant();
        }
        if (role == Qt::ForegroundRole) {
            // Red if overwrite dir has files, muted yellow if empty
            QString owDir = AppConfig::dataRoot() + "/overwrite";
            QDirIterator it(owDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            bool hasFiles = it.hasNext();
            return hasFiles ? QColor(Qt::red) : QColor(Qt::darkYellow);
        }
        if (role == Qt::FontRole) {
            QString owDir = AppConfig::dataRoot() + "/overwrite";
            QDirIterator it(owDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            bool hasFiles = it.hasNext();
            QFont f; f.setBold(hasFiles); return f;
        }
        return {};
    }

    if (raw < 0) return {};
    const auto& entry = m_profile->modList().at(raw);
    bool isSep = (entry.type == EntryType::Separator);

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
            case ColPriority: return isSep ? QVariant() : QVariant(raw);
            case ColName:
                if (isSep) {
                    QString arrow = entry.collapsed ? "\xe2\x96\xb6" : "\xe2\x96\xbc";
                    return QString("%1  %2").arg(arrow, entry.name);
                }
                return entry.name;
            case ColVersion: return isSep ? QVariant() : entry.version;
            case ColFlags: {
                if (isSep) return QString();
                QStringList parts;
                if (entry.isOutputMod) parts << "Output";
                if (entry.hasFomodChoices) parts << "FOMOD";
                QString flags = parts.join(" ");
                if (m_depWarnings.contains(entry.id)) flags = "\xe2\x9a\xa0 " + flags; // ⚠
                return flags;
            }
            default: return {};
        }
    }
    if (role == Qt::ToolTipRole && !isSep && m_depWarnings.contains(entry.id))
        return m_depWarnings.value(entry.id).join("\n");
    if (role == Qt::CheckStateRole && idx.column() == ColEnabled && !isSep)
        return entry.enabled ? Qt::Checked : Qt::Unchecked;
    if (role == Qt::DecorationRole && isSep && idx.column() == ColName && !entry.icon.isEmpty()) {
        QColor tint = (entry.iconColored && !entry.color.isEmpty())
                          ? QColor(entry.color) : QColor(Qt::white);
        return renderSvgIcon(entry.icon, tint, 20);
    }
    if (role == Qt::FontRole && isSep) {
        QFont f; f.setBold(true); return f;
    }
    if (role == Qt::FontRole && !isSep && entry.type == EntryType::Mod && isModEmpty(entry.id)) {
        QFont f; f.setItalic(true); return f;
    }
    if (role == Qt::BackgroundRole && isSep && !entry.color.isEmpty())
        return QColor(entry.color).lighter(170);
    if (role == Qt::ForegroundRole && isSep && !entry.color.isEmpty())
        return QColor(entry.color).darker(150);
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
        if (cur.type != EntryType::Mod) return false;
        QString nn = value.toString().trimmed();
        if (nn.isEmpty()) return false;
        ModEntry e = cur;
        e.name = nn;
        m_profile->modList().update(e.id, e);
        m_profile->save();
        emit dataChanged(idx, idx);
        emit modsChanged();
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
        if (entry.type == EntryType::Mod && idx.column() == ColEnabled)
            f |= Qt::ItemIsUserCheckable;
        if (entry.type == EntryType::Mod && idx.column() == ColName)
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
    int dstRaw = rawIndexForRow(dst);
    if (srcRaw < 0 || dstRaw < 0) return false;
    beginMoveRows({}, src, src, {}, dst > src ? dst + 1 : dst);
    m_profile->modList().move(srcRaw, dstRaw);
    m_profile->save();
    rebuildVisibleRows();
    endMoveRows();
    emit modsChanged();
    return true;
}

} // namespace solero
