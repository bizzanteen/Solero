#include "PluginListModel.h"
#include "core/AppConfig.h"
#include "install/PluginScanner.h"
#include <QFont>
#include <QMimeData>
#include <QByteArray>
#include <QStringList>

namespace solero {

PluginListModel::PluginListModel(QObject* parent) : QAbstractTableModel(parent) {}

void PluginListModel::setProfile(Profile* profile) {
    beginResetModel(); m_profile = profile; endResetModel();
}

// Load-order band of a plugin: masters (0) sort before light masters (1),
// which sort before regular plugins (2). Light masters take precedence over
// the plain master flag so ESL-flagged files are grouped with other lights.
static int pluginBand(const PluginEntry& p) {
    if (p.isLight) return 1;
    if (p.isMaster) return 0;
    return 2;
}

void PluginListModel::reconcile(const QStringList& available) {
    if (!m_profile) return;
    beginResetModel();
    PluginList& pl = m_profile->pluginList();
    const QString dataDir = AppConfig::instance().gameDir() + "/Data";

    // Build the rebuilt list as a vector so we can insert in the correct band.
    QList<PluginEntry> entries;
    // Keep current order (and enabled state) for plugins still available.
    for (int i = 0; i < pl.count(); ++i) {
        const auto& p = pl.at(i);
        if (available.contains(p.filename, Qt::CaseInsensitive)) entries.append(p);
    }
    // Insert newly-available plugins not already present, each into its band.
    for (const QString& fn : available) {
        bool present = false;
        for (const auto& e : entries)
            if (e.filename.compare(fn, Qt::CaseInsensitive) == 0) { present = true; break; }
        if (present) continue;

        PluginEntry pe;
        pe.filename = fn;
        pe.enabled  = true; // freshly installed mods default to active
        PluginFlags pf = PluginScanner::readFlags(dataDir + "/" + fn);
        if (pf.ok) {
            pe.isMaster = pf.isMaster;
            pe.isLight  = pf.isLight;
        } else {
            pe.isMaster = fn.endsWith(".esm", Qt::CaseInsensitive);
            pe.isLight  = fn.endsWith(".esl", Qt::CaseInsensitive);
        }

        // Insert after the last existing entry whose band is <= this band, so a
        // new master lands among masters, a light among lights, an esp at end.
        const int band = pluginBand(pe);
        int insertAt = entries.size();
        for (int i = 0; i < entries.size(); ++i) {
            if (pluginBand(entries[i]) > band) { insertAt = i; break; }
        }
        entries.insert(insertAt, pe);
    }

    PluginList rebuilt;
    for (const auto& e : entries) rebuilt.append(e);
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

    if (role == Qt::TextAlignmentRole && idx.column() == ColPriority)
        return QVariant(Qt::AlignCenter);

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
            case ColPriority: return idx.row();
            case ColName:     return p.filename;
            case ColFlags: {
                if (p.isMaster || p.filename.endsWith(".esm", Qt::CaseInsensitive))
                    return QStringLiteral("ESM");
                if (p.isLight || p.filename.endsWith(".esl", Qt::CaseInsensitive))
                    return QStringLiteral("ESL");
                return QStringLiteral("ESP");
            }
            default: return {};
        }
    }
    if (role == Qt::CheckStateRole && idx.column() == ColEnabled)
        return p.enabled ? Qt::Checked : Qt::Unchecked;
    if (role == Qt::FontRole && idx.row() < m_profile->pluginList().count()) {
        const auto& fp = m_profile->pluginList().at(idx.row());
        QFont f;
        if (fp.isMaster) f.setBold(true);
        else if (fp.isLight) f.setItalic(true);
        else if (fp.filename.endsWith(".esm", Qt::CaseInsensitive)) f.setBold(true);
        else if (fp.filename.endsWith(".esl", Qt::CaseInsensitive)) f.setItalic(true);
        return f;
    }
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

static const char* kPluginMime = "application/x-solero-plugin-row";

QStringList PluginListModel::mimeTypes() const { return { QString::fromLatin1(kPluginMime) }; }

QMimeData* PluginListModel::mimeData(const QModelIndexList& indexes) const {
    auto* mime = new QMimeData;
    int row = -1;
    for (const auto& idx : indexes) { if (idx.isValid()) { row = idx.row(); break; } }
    mime->setData(QString::fromLatin1(kPluginMime), QByteArray::number(row));
    return mime;
}

bool PluginListModel::canDropMimeData(const QMimeData* data, Qt::DropAction, int, int, const QModelIndex&) const {
    return data->hasFormat(QString::fromLatin1(kPluginMime));
}

bool PluginListModel::dropMimeData(const QMimeData* data, Qt::DropAction, int row, int, const QModelIndex& parent) {
    if (!m_profile || !data->hasFormat(QString::fromLatin1(kPluginMime))) return false;
    int src = data->data(QString::fromLatin1(kPluginMime)).toInt();
    int n = m_profile->pluginList().count();
    if (src < 0 || src >= n) return false;
    int dst = row;
    if (dst < 0) dst = parent.isValid() ? parent.row() : n; // dropped onto a row, or past the end
    if (dst > n) dst = n;
    // Account for the source being removed before re-insertion.
    if (dst > src) dst -= 1;
    if (dst == src) return false;
    beginResetModel();
    m_profile->pluginList().move(src, dst);
    m_profile->save();
    endResetModel();
    return false; // we performed the move ourselves; return false so the view doesn't also removeRows
}

} // namespace solero
