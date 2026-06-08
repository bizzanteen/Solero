#include "PluginListModel.h"
#include "core/AppConfig.h"
#include "install/PluginScanner.h"
#include "IconUtil.h"
#include <QFont>
#include <QBrush>
#include <QMimeData>
#include <QByteArray>
#include <QStringList>
#include <QSet>
#include <algorithm>

namespace solero {

namespace {
// U+1F4CC PUSHPIN, shown on pinned rows. QChar holds a single UTF-16 code unit
// and can't represent this non-BMP code point, so build the QString straight
// from the code point (no \xNN byte escapes - those have caused mojibake here).
const QString& pinGlyph() {
    static const QString g = [] {
        const char32_t cp = 0x1F4CC;
        return QString::fromUcs4(&cp, 1);
    }();
    return g;
}
} // namespace

PluginListModel::PluginListModel(QObject* parent) : QAbstractTableModel(parent) {}

void PluginListModel::setProfile(Profile* profile) {
    beginResetModel(); m_profile = profile; endResetModel();
}

// pluginBand() now lives in core/PluginList (shared with the DnD rule logic).

void PluginListModel::reconcile(const QStringList& available) {
    if (!m_profile) return;
    beginResetModel();
    PluginList& pl = m_profile->pluginList();
    const QString gameDir = AppConfig::instance().gameDir();
    const QString dataDir = gameDir + "/Data";

    // Official load order (base masters + Skyrim.ccc) computed once per reconcile.
    const QStringList official = PluginScanner::officialPlugins(gameDir);
    auto officialRank = [&](const QString& fn) -> int {
        for (int i = 0; i < official.size(); ++i)
            if (fn.compare(official[i], Qt::CaseInsensitive) == 0) return i;
        return -1;
    };
    // Refresh masters + isOfficial on an entry from disk + the official set.
    auto refreshMeta = [&](PluginEntry& pe) {
        pe.masters    = PluginScanner::readMasters(dataDir + "/" + pe.filename);
        pe.isOfficial = officialRank(pe.filename) >= 0;
    };

    // Build the rebuilt list as a vector so we can insert in the correct band.
    QList<PluginEntry> entries;
    // Keep current order (and enabled state) for plugins still available.
    for (int i = 0; i < pl.count(); ++i) {
        auto p = pl.at(i);
        if (available.contains(p.filename, Qt::CaseInsensitive)) {
            refreshMeta(p); // cheap refresh of masters/isOfficial for kept entries
            entries.append(p);
        }
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
        refreshMeta(pe);

        // Insert after the last existing entry whose band is <= this band, so a
        // new master lands among masters, a light among lights, an esp at end.
        const int band = pluginBand(pe);
        int insertAt = entries.size();
        for (int i = 0; i < entries.size(); ++i) {
            if (pluginBand(entries[i]) > band) { insertAt = i; break; }
        }
        entries.insert(insertAt, pe);
    }

    // Force official plugins to the top block, in official-list order; the rest
    // keep their relative order below.
    QList<PluginEntry> officials, rest;
    for (const auto& e : entries) {
        if (e.isOfficial) officials.append(e);
        else rest.append(e);
    }
    std::stable_sort(officials.begin(), officials.end(),
        [&](const PluginEntry& a, const PluginEntry& b) {
            return officialRank(a.filename) < officialRank(b.filename);
        });

    PluginList rebuilt;
    for (const auto& e : officials) rebuilt.append(e);
    for (const auto& e : rest)      rebuilt.append(e);
    rebuilt.copyOrderState(pl); // carry lock + pins across the wholesale rebuild
    pl = rebuilt;
    pl.applyPins(); // restore pinned plugins to their indices after the reconcile
    endResetModel();
}

void PluginListModel::setAllEnabled(bool enabled) {
    if (!m_profile) return;
    PluginList& pl = m_profile->pluginList();
    if (pl.count() == 0) return;
    for (int i = 0; i < pl.count(); ++i)
        pl.setEnabled(pl.at(i).filename, enabled);
    m_profile->save();
    emit dataChanged(index(0, 0), index(pl.count() - 1, ColCount - 1));
}

void PluginListModel::togglePin(int row) {
    if (!m_profile) return;
    PluginList& pl = m_profile->pluginList();
    if (row < 0 || row >= pl.count()) return;
    const QString fn = pl.at(row).filename;
    pl.setPinned(fn, !pl.isPinned(fn));
    m_profile->save();
    emit dataChanged(index(row, 0), index(row, ColCount - 1));
}

bool PluginListModel::isRowPinned(int row) const {
    if (!m_profile || row < 0 || row >= m_profile->pluginList().count()) return false;
    const auto& p = m_profile->pluginList().at(row);
    return m_profile->pluginList().isPinned(p.filename);
}

bool PluginListModel::isRowOfficial(int row) const {
    if (!m_profile || row < 0 || row >= m_profile->pluginList().count()) return false;
    return m_profile->pluginList().at(row).isOfficial;
}

void PluginListModel::setHighlighted(const QSet<QString>& lowerFilenames) {
    m_highlight = lowerFilenames;
    if (rowCount() > 0) emit dataChanged(index(0, 0), index(rowCount() - 1, ColCount - 1));
}

int PluginListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !m_profile) return 0;
    return m_profile->pluginList().count();
}

// Master filenames (lowercased) declared by `p` that are not present in the
// current plugin list - i.e. missing dependencies.
QStringList PluginListModel::missingMasters(const PluginEntry& p) const {
    // A disabled plugin isn't loaded, so its missing masters aren't a problem -
    // no red text, no tooltip warning, and (via HealthCheck) no Problems entry.
    if (!p.enabled || p.masters.isEmpty() || !m_profile) return {};
    QSet<QString> present;
    const PluginList& pl = m_profile->pluginList();
    for (int i = 0; i < pl.count(); ++i) present.insert(pl.at(i).filename.toLower());
    QStringList missing;
    for (const QString& m : p.masters)
        if (!present.contains(m.toLower())) missing << m;
    return missing;
}

QVariant PluginListModel::data(const QModelIndex& idx, int role) const {
    if (!m_profile || idx.row() >= m_profile->pluginList().count()) return {};
    const auto& p = m_profile->pluginList().at(idx.row());

    if (role == Qt::TextAlignmentRole && idx.column() == ColPriority)
        return QVariant(Qt::AlignCenter);

    // Missing masters are now surfaced as red text (Qt::ForegroundRole below),
    // not a decoration icon - so no DecorationRole is returned for them.
    if (role == Qt::ForegroundRole && idx.column() == ColName) {
        if (!missingMasters(p).isEmpty()) return QBrush(QColor(0xD0, 0x40, 0x40));
        return {};
    }
    if (role == Qt::ToolTipRole) {
        QStringList parts;
        const PluginList& pl = m_profile->pluginList();
        if (pl.isPinned(p.filename))
            parts << (pinGlyph() + QStringLiteral(" Pinned - restored to index %1 after sorts")
                                       .arg(pl.pinnedIndex(p.filename)));
        const QStringList missing = missingMasters(p);
        if (!missing.isEmpty()) {
            QString t = QStringLiteral("Missing masters:");
            for (const QString& m : missing) t += "\n - " + m;
            parts << t;
        } else if (!p.masters.isEmpty()) {
            QString t = QStringLiteral("Masters:");
            for (const QString& m : p.masters) t += "\n - " + m;
            parts << t;
        }
        return parts.join(QStringLiteral("\n\n"));
    }

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
            case ColPriority: return idx.row();
            case ColName:
                return m_profile->pluginList().isPinned(p.filename)
                    ? (pinGlyph() + QStringLiteral(" ") + p.filename)
                    : p.filename;
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
        return (p.isOfficial || p.enabled) ? Qt::Checked : Qt::Unchecked;
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
    if (idx.row() < 0 || idx.row() >= m_profile->pluginList().count()) return false;
    if (role == Qt::CheckStateRole && idx.column() == ColEnabled) {
        const auto& p = m_profile->pluginList().at(idx.row());
        m_profile->pluginList().setEnabled(p.filename, value.toInt() == Qt::Checked);
        m_profile->save();
        emit dataChanged(idx, idx, {role});
        // Toggling enabled changes this plugin's missing-master state, so repaint
        // the Name column (red text / tooltip) for every row immediately, and let
        // the health indicator / Problems panel recompute live.
        const int last = m_profile->pluginList().count() - 1;
        if (last >= 0)
            emit dataChanged(index(0, ColName), index(last, ColName),
                             {Qt::ForegroundRole, Qt::ToolTipRole, Qt::DisplayRole});
        emit pluginEnabledChanged();
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
    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    bool official = false;
    if (m_profile && idx.row() >= 0 && idx.row() < m_profile->pluginList().count())
        official = m_profile->pluginList().at(idx.row()).isOfficial;
    if (!official) {
        f |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
        if (idx.column() == ColEnabled) f |= Qt::ItemIsUserCheckable;
    } else {
        // Official plugins are locked. Let the drag START (so the user gets a
        // "no-drop" cursor as feedback - allowedDropRange rejects every target),
        // but they aren't drop targets and aren't user-checkable. Render the
        // always-on checkbox greyed by disabling just that cell.
        f |= Qt::ItemIsDragEnabled;
        if (idx.column() == ColEnabled) f &= ~Qt::ItemIsEnabled;
    }
    return f;
}

Qt::DropActions PluginListModel::supportedDropActions() const { return Qt::MoveAction; }

static const char* kPluginMime = "application/x-solero-plugin-row";

QStringList PluginListModel::mimeTypes() const { return { QString::fromLatin1(kPluginMime) }; }

QMimeData* PluginListModel::mimeData(const QModelIndexList& indexes) const {
    auto* mime = new QMimeData;
    int row = -1;
    for (const auto& idx : indexes) { if (idx.isValid()) { row = idx.row(); break; } }
    mime->setData(QString::fromLatin1(kPluginMime), QByteArray::number(row));
    return mime;
}

// Map a drop (row/parent, in pre-removal view coords) + source row to the
// resulting move() destination index (post-removal coords). Returns -1 for a
// no-op or out-of-range source.
static int resolveFinalTo(int src, int n, int row, const QModelIndex& parent) {
    if (src < 0 || src >= n) return -1;
    int insertion = row;
    if (insertion < 0) insertion = parent.isValid() ? parent.row() : n;
    if (insertion < 0) insertion = 0;
    if (insertion > n) insertion = n;
    const int finalTo = (insertion > src) ? insertion - 1 : insertion;
    if (finalTo == src) return -1; // no-op
    return finalTo;
}

bool PluginListModel::canDropMimeData(const QMimeData* data, Qt::DropAction,
                                      int row, int, const QModelIndex& parent) const {
    if (!m_profile || !data->hasFormat(QString::fromLatin1(kPluginMime))) return false;
    const int src = data->data(QString::fromLatin1(kPluginMime)).toInt();
    const int n = m_profile->pluginList().count();
    const int finalTo = resolveFinalTo(src, n, row, parent);
    if (finalTo < 0) return false; // no-op / invalid source -> don't show indicator
    // Reflect the load-order rules in the drop indicator: reject illegal targets.
    return m_profile->pluginList().isValidMove(src, finalTo);
}

bool PluginListModel::dropMimeData(const QMimeData* data, Qt::DropAction, int row, int, const QModelIndex& parent) {
    if (!m_profile || !data->hasFormat(QString::fromLatin1(kPluginMime))) return false;
    const int src = data->data(QString::fromLatin1(kPluginMime)).toInt();
    const int n = m_profile->pluginList().count();
    const int finalTo = resolveFinalTo(src, n, row, parent);
    if (finalTo < 0) return false;

    // Enforce valid load order: reject (no change, row snaps back) any move that
    // would cross the locked/official block, break the master<light<esp band
    // order, or break master-before-dependents ordering.
    if (!m_profile->pluginList().isValidMove(src, finalTo))
        return false;

    // beginMoveRows takes the INSERTION point (pre-removal coords); reconstruct
    // it from finalTo for the call.
    const int insertion = (finalTo >= src) ? finalTo + 1 : finalTo;

    // Use beginMoveRows (not beginResetModel): resetting the model from inside the
    // view's drop event invalidates the persistent indexes the view still holds,
    // which crashes on the next move. moveRows updates them correctly.
    // Capture the dragged plugin's name before the move so a pinned plugin can
    // have its pinned index updated to the slot the user just dragged it to
    // (pins follow the user's own moves; only sorts/reconciles restore them).
    const QString movedFn = m_profile->pluginList().at(src).filename;

    if (!beginMoveRows(QModelIndex(), src, src, QModelIndex(), insertion))
        return false;
    m_profile->pluginList().move(src, finalTo);
    if (m_profile->pluginList().isPinned(movedFn))
        m_profile->pluginList().setPinned(movedFn, true); // re-record at new index
    m_profile->save();
    endMoveRows();
    emit loadOrderChanged(); // a successful manual reorder marks the order dirty
    return false; // we performed the move; return false so the view doesn't also removeRows
}

} // namespace solero
