#include "PluginListModel.h"

namespace solero {

PluginListModel::PluginListModel(QObject* parent) : QAbstractTableModel(parent) {}

void PluginListModel::setProfile(Profile* profile) {
    beginResetModel(); m_profile = profile; endResetModel();
}

void PluginListModel::reconcile(const QStringList& available) {
    if (!m_profile) return;
    beginResetModel();
    PluginList& pl = m_profile->pluginList();
    PluginList rebuilt;
    // Keep current order for plugins that are still available.
    for (int i = 0; i < pl.count(); ++i) {
        const auto& p = pl.at(i);
        if (available.contains(p.filename, Qt::CaseInsensitive)) rebuilt.append(p);
    }
    // Append newly-available plugins not already present.
    for (const QString& fn : available) {
        if (!rebuilt.findByFilename(fn)) {
            PluginEntry pe;
            pe.filename = fn; pe.enabled = true;
            pe.isMaster = fn.endsWith(".esm", Qt::CaseInsensitive);
            pe.isLight  = fn.endsWith(".esl", Qt::CaseInsensitive);
            rebuilt.append(pe);
        }
    }
    pl = rebuilt;
    endResetModel();
}

void PluginListModel::setHighlighted(const QSet<QString>& lowerFilenames) {
    m_highlight = lowerFilenames;
    if (rowCount() > 0) emit dataChanged(index(0, 0), index(rowCount() - 1, ColCount - 1));
}

int PluginListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !m_profile) return 0;
    return m_profile->pluginList().count();
}

QVariant PluginListModel::data(const QModelIndex& idx, int role) const {
    if (!m_profile || idx.row() >= m_profile->pluginList().count()) return {};
    const auto& p = m_profile->pluginList().at(idx.row());

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
            case ColPriority: return idx.row();
            case ColName:     return p.filename;
            case ColFlags: {
                QStringList f;
                if (p.isMaster) f << "ESM";
                if (p.isLight)  f << "ESL";
                return f.join(' ');
            }
            default: return {};
        }
    }
    if (role == Qt::CheckStateRole && idx.column() == ColEnabled)
        return p.enabled ? Qt::Checked : Qt::Unchecked;
    if (role == Qt::BackgroundRole && m_profile && idx.row() < m_profile->pluginList().count()) {
        const auto& hp = m_profile->pluginList().at(idx.row());
        if (m_highlight.contains(hp.filename.toLower())) return QColor("#3d5a80");
    }
    return {};
}

bool PluginListModel::setData(const QModelIndex& idx, const QVariant& value, int role) {
    if (!m_profile) return false;
    if (role == Qt::CheckStateRole && idx.column() == ColEnabled) {
        const auto& p = m_profile->pluginList().at(idx.row());
        m_profile->pluginList().setEnabled(p.filename, value.toInt() == Qt::Checked);
        m_profile->save();
        emit dataChanged(idx, idx, {role});
        return true;
    }
    return false;
}

QVariant PluginListModel::headerData(int s, Qt::Orientation, int role) const {
    if (role != Qt::DisplayRole) return {};
    switch (s) {
        case ColEnabled:  return "";
        case ColPriority: return "#";
        case ColName:     return "Plugin";
        case ColFlags:    return "Type";
        default: return {};
    }
}

Qt::ItemFlags PluginListModel::flags(const QModelIndex& idx) const {
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                      Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    if (idx.column() == ColEnabled) f |= Qt::ItemIsUserCheckable;
    return f;
}

Qt::DropActions PluginListModel::supportedDropActions() const { return Qt::MoveAction; }

bool PluginListModel::moveRows(const QModelIndex&, int src, int, const QModelIndex&, int dst) {
    if (!m_profile) return false;
    beginMoveRows({}, src, src, {}, dst > src ? dst + 1 : dst);
    m_profile->pluginList().move(src, dst);
    m_profile->save();
    endMoveRows();
    return true;
}

} // namespace solero
