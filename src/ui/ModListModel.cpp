#include "ModListModel.h"
#include <QColor>
#include <QFont>
#include <QDir>
#include <QDirIterator>
#include <QMimeData>
#include <QByteArray>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "core/AppConfig.h"
#include "core/StagingFolder.h"
#include "core/VersionUtil.h"
#include "ui/IconUtil.h"
#include <QSet>
#include <QDebug>

namespace solero {

namespace {
// Read back a reconstructed/recorded FOMOD selection as human-readable lines
// ("Step - A, B") from fomod-choices.json. Empty when the file is absent/empty.
QString fomodChoicesSummary(const QString& modId) {
    QFile f(AppConfig::dataRoot() + "/fomod-choices/" + modId + ".json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonArray steps = QJsonDocument::fromJson(f.readAll())
                                 .object().value("steps").toArray();
    QStringList lines;
    for (const QJsonValue& sv : steps) {
        const QJsonObject so = sv.toObject();
        QStringList picks;
        for (const QJsonValue& pv : so.value("selected").toArray())
            picks << pv.toString();
        if (picks.isEmpty()) continue;
        const QString step = so.value("step").toString();
        lines << (step.isEmpty() ? picks.join(", ")
                                 : QString("%1 - %2").arg(step, picks.join(", ")));
    }
    return lines.join("\n");
}
} // namespace

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
    // Undo history must not cross profiles.
    clearUndoRedo();
}

void ModListModel::setUpdateInfo(const QHash<QString, QPair<QString,QString>>& info) {
    m_updates = info;
    if (rowCount() > 0)
        emit dataChanged(index(0,0), index(rowCount()-1, ColCount-1));
}

void ModListModel::setConflictHighlights(const QHash<QString,int>& roles) {
    if (m_conflictHi == roles) return;
    m_conflictHi = roles;
    if (rowCount() > 0)
        emit dataChanged(index(0,0), index(rowCount()-1, ColCount-1), {Qt::BackgroundRole});
}

void ModListModel::setConflictIndex(const ConflictIndex& ci) {
    m_conflicts = ci;
    m_overwritingMods.clear();
    m_overwrittenMods.clear();
    // Precompute per-mod winner/loser membership once so data() stays O(1).
    for (const QString& path : ci.conflictedPaths()) {
        const QString w = ci.winnerOf(path);
        if (!w.isEmpty()) m_overwritingMods.insert(w);
        for (const QString& l : ci.losersOf(path)) m_overwrittenMods.insert(l);
    }
    if (rowCount() > 0)
        emit dataChanged(index(0,0), index(rowCount()-1, ColCount-1));
}

void ModListModel::refreshFlags() {
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

void ModListModel::setSearchExpandAll(bool on) {
    if (m_searchExpandAll == on) return;
    beginResetModel();
    m_searchExpandAll = on;
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
    const QString folder = m_profile ? m_profile->stagingFolderFor(id) : id;
    QDirIterator di(AppConfig::instance().stagingDir() + "/" + folder,
                    QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    bool empty = !di.hasNext();
    m_emptyCache.insert(id, empty);
    return empty;
}

void ModListModel::rebuildVisibleRows() {
    m_visibleRows.clear();
    m_priorityByRaw.clear();
    // Keep m_emptyCache / m_overwriteHasFiles persistent: rebuild() is called on
    // enable/move/rename/collapse - none of which change staged files. Caches are
    // only dropped in setProfile() or via invalidateModCache().
    if (!m_profile) return;

    // Search mode: ignore collapse entirely so a name/state filter can reach mods
    // inside collapsed separators / group parents. Every entry is a visible row;
    // persisted collapse state is left untouched (this branch never reads it).
    if (m_searchExpandAll) {
        int modPos = 0;
        for (int i = 0; i < m_profile->modList().count(); ++i) {
            const auto& e = m_profile->modList().at(i);
            if (e.type == EntryType::Mod)
                m_priorityByRaw.insert(i, ++modPos);
            m_visibleRows.append(i);
        }
        m_visibleRows.append(-1); // Overwrite always at bottom
        return;
    }

    // Level-aware separator collapse: collapsedLevel holds the depth of the
    // currently-collapsing separator (or -1 = nothing collapsed). A separator
    // deeper than collapsedLevel sits inside a collapsed ancestor -> hidden; a
    // separator at the same or shallower level ends that collapse. Mods are hidden
    // whenever collapsedLevel >= 0. With every separator at level 0 this reduces to
    // the original flat behaviour.
    int collapsedLevel = -1;
    QString curParentId;       // id of the current top-level parent mod, if any
    bool curParentCollapsed = false;
    int modPos = 0;            // running 1-based Mod position, raw order
    for (int i = 0; i < m_profile->modList().count(); ++i) {
        const auto& e = m_profile->modList().at(i);
        if (e.type == EntryType::Mod)
            ++modPos;
        if (e.type == EntryType::Mod)
            m_priorityByRaw.insert(i, modPos);
        if (e.type == EntryType::Separator) {
            curParentId.clear();
            if (collapsedLevel >= 0 && e.separatorLevel > collapsedLevel)
                continue; // inside a collapsed ancestor -> hidden (collapse persists)
            // Visible: this separator ends any shallower/equal collapse and may
            // start a new one if it is itself collapsed.
            m_visibleRows.append(i);
            collapsedLevel = e.collapsed ? e.separatorLevel : -1;
        } else if (!e.parentId.isEmpty()) {
            // Child mod: hidden inside a collapsed separator, or when its parent is
            // collapsed (matched by parentId == the tracked top-level parent's id).
            bool hiddenByParent = (e.parentId == curParentId && curParentCollapsed);
            if (collapsedLevel < 0 && !hiddenByParent)
                m_visibleRows.append(i);
        } else {
            // Top-level mod (possibly a group parent). Remember it so following
            // children can be hidden when it's collapsed.
            curParentId = e.id;
            curParentCollapsed = e.collapsed;
            if (collapsedLevel < 0)
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

int ModListModel::rowForModId(const QString& id) const {
    if (!m_profile || id.isEmpty()) return -1;
    const auto& list = m_profile->modList();
    int raw = -1;
    for (int i = 0; i < list.count(); ++i)
        if (list.at(i).id == id) { raw = i; break; }
    if (raw < 0) return -1;
    return rawToVisible(raw); // -1 if hidden (collapsed group/separator)
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
        // Hand the active profile's name to the ColName delegate for the styled suffix.
        if (role == OverwriteProfileRole && idx.column() == ColName)
            return m_profile ? m_profile->name() : QString();
        if (role == Qt::ForegroundRole || role == Qt::FontRole) {
            if (m_overwriteHasFiles < 0) {
                QString owDir = AppConfig::overwriteDir(m_profile ? m_profile->name() : QString());
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

    // Separators render full-width in the spanned column 0: their "arrow + name"
    // label, icon and edit text all live on ColEnabled, not ColName.
    if (isSep && role == Qt::DisplayRole && idx.column() == ColEnabled) {
        QString arrow = entry.collapsed ? "\xe2\x96\xb6" : "\xe2\x96\xbc";
        // Sub-categories are inset: 4 leading spaces per level (mirrored by the
        // arrow hit-test in ModListView::mousePressEvent). Spaces precede the
        // arrow, so the disclosure glyph itself shifts right with the indent.
        const QString indent(entry.separatorLevel * 4, QChar(' '));
        return indent + QString("%1  %2").arg(arrow, entry.name);
    }

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
            case ColPriority: {
                if (isSep) return QVariant();
                // Precomputed in rebuildVisibleRows(): 1-based contiguous position
                // among mod-type entries (raw order, children included). O(1).
                return m_priorityByRaw.value(raw, 0);
            }
            case ColName:
                if (isSep) return QVariant(); // separator label lives on ColEnabled
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
                    return u.first + " \xe2\x86\x92 " + u.second; // installed -> latest (already normalized)
                }
                // Display a clean version (strip MO2's ".0" padding / variant tails).
                return normalizeVersion(entry.version);
            case ColFlags: {
                if (isSep) return QString();
                // Status icons (conflicts / note / missing-dep / update) render via
                // DecorationRole; this textual part keeps the mod-kind labels.
                QStringList parts;
                if (entry.isOutputMod) parts << "Output";
                if (entry.hasFomodChoices) parts << "FOMOD";
                return parts.join(" ");
            }
            default: return {};
        }
    }
    if (role == Qt::ToolTipRole && !isSep) {
        QStringList tips;
        if (m_overwritingMods.contains(entry.id))
            tips << (QChar(0x25B2) + QStringLiteral(" Overwrites other mods (wins file conflicts)"));
        if (m_overwrittenMods.contains(entry.id))
            tips << (QChar(0x25BC) + QStringLiteral(" Overwritten by other mods (loses file conflicts)"));
        if (m_depWarnings.contains(entry.id))
            tips << m_depWarnings.value(entry.id).join("\n");
        if (m_updates.contains(entry.id)) {
            const auto& u = m_updates.value(entry.id);
            tips << QString("Update available: %1 \xe2\x86\x92 %2").arg(u.first, u.second);
        }
        if (!entry.note.isEmpty())
            tips << QString::fromUtf8("\xf0\x9f\x93\x9d Note: ") + entry.note; // 📝 (non-BMP -> fromUtf8)
        if (entry.isFomod) {
            if (entry.fomodStatus == "needs-rerun") {
                tips << (QStringLiteral("FOMOD installer ") + QChar('-')
                    + QStringLiteral(" choices not reconstructable; "
                                     "re-run the installer to record them"));
            } else {
                const QString sum = fomodChoicesSummary(entry.id);
                tips << (sum.isEmpty()
                             ? QStringLiteral("FOMOD installer")
                             : QStringLiteral("FOMOD choices:\n") + sum);
            }
        }
        if (m_profile) {
            const int hiddenCount = m_profile->hiddenFiles().value(entry.id).size();
            if (hiddenCount > 0)
                tips << QString("\xf0\x9f\x91\x81 %1 hidden file(s)").arg(hiddenCount); // 👁
            int forcedWins = 0;
            for (auto it = m_profile->fileOverrides().cbegin();
                 it != m_profile->fileOverrides().cend(); ++it)
                if (it.value() == entry.id) ++forcedWins;
            if (forcedWins > 0)
                tips << QString("\xe2\x98\x85 Forced winner on %1 path(s)").arg(forcedWins); // ★
        }
        if (!tips.isEmpty()) return tips.join("\n");
    }
    if (role == Qt::CheckStateRole && idx.column() == ColEnabled && !isSep)
        return entry.enabled ? Qt::Checked : Qt::Unchecked;
    // Separators rename via their spanned column 0; mods rename via ColName.
    if (role == Qt::EditRole && isSep && idx.column() == ColEnabled)
        return entry.name;
    if (role == Qt::EditRole && !isSep && idx.column() == ColName)
        return entry.name;
    if (role == Qt::DecorationRole && isSep && idx.column() == ColEnabled && !entry.icon.isEmpty()) {
        return renderSvgIcon(entry.icon, solero::contrastText(QColor(entry.color)), 20);
    }
    // Out-of-date mod entries: a yellow up-arrow next to the VERSION.
    if (role == Qt::DecorationRole && !isSep && idx.column() == ColVersion
            && entry.type == EntryType::Mod && m_updates.contains(entry.id)) {
        return solero::yellowUpArrowIcon();
    }
    // Persistent Flags column: always-on status icons composed into one cell.
    if (role == Qt::DecorationRole && !isSep && idx.column() == ColFlags
            && entry.type == EntryType::Mod) {
        QList<QIcon> icons;
        if (m_overwritingMods.contains(entry.id)) icons << solero::greenUpTriangleIcon();
        if (m_overwrittenMods.contains(entry.id)) icons << solero::redDownTriangleIcon();
        if (m_depWarnings.contains(entry.id))      icons << solero::redBangIcon(solero::kFlagIconPx);
        if (m_updates.contains(entry.id))          icons << solero::yellowUpArrowIcon();
        if (!entry.note.isEmpty())                 icons << solero::noteIcon();
        if (entry.isFomod)                         icons << solero::fomodIcon(entry.fomodStatus);
        if (icons.isEmpty()) return {};
        return solero::composeIcons(icons);
    }
    if (role == Qt::FontRole && isSep) {
        QFont f; f.setBold(true); return f;
    }
    if (role == Qt::FontRole && !isSep && entry.type == EntryType::Mod && isModEmpty(entry.id)) {
        QFont f; f.setItalic(true); return f;
    }
    // MO2-style conflict highlight (when a mod is selected): green = this mod
    // overwrites the selected one; red = it is overwritten by the selected one.
    if (role == Qt::BackgroundRole && !isSep && entry.type == EntryType::Mod) {
        auto ci = m_conflictHi.constFind(entry.id);
        if (ci != m_conflictHi.constEnd())
            return ci.value() == 1 ? QColor(0x2e, 0x5d, 0x34)   // green: overwrites selected (wins over it)
                                   : QColor(0x6b, 0x2e, 0x2e);  // red: overwritten by selected (loses to it)
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
    // Mods rename via ColName; separators rename via their spanned column 0.
    const auto& curEntry = m_profile->modList().at(raw);
    bool renameCol = (curEntry.type == EntryType::Separator)
                         ? (idx.column() == ColEnabled)
                         : (idx.column() == ColName);
    if (role == Qt::EditRole && renameCol) {
        const auto& cur = m_profile->modList().at(raw);
        QString nn = value.toString().trimmed();
        if (nn.isEmpty()) return false;
        ModEntry e = cur;
        e.name = nn;
        // For MODS only, keep the on-disk staging folder name-based: derive a new
        // unique folder from the new name and rename it on disk. Separators have no
        // staging folder, so they keep the simple rename above.
        if (cur.type == EntryType::Mod) {
            const QString stagingDir = AppConfig::instance().stagingDir();
            // Every OTHER mod's folder is "taken" (lowercased), so the new folder
            // can't collide. Skip the entry being renamed (its own old folder is
            // free to give up).
            QSet<QString> taken;
            for (const auto& other : m_profile->modList()) {
                if (other.id == e.id) continue;
                if (other.type != EntryType::Mod) continue;
                const QString f = other.stagingFolder.isEmpty() ? other.id
                                                                : other.stagingFolder;
                if (!f.isEmpty()) taken.insert(f.toLower());
            }
            const QString oldFolder =
                cur.stagingFolder.isEmpty() ? cur.id : cur.stagingFolder;
            const QString newFolder =
                solero::uniqueStagingFolder(solero::sanitizeStagingFolder(nn), taken);
            if (newFolder != oldFolder) {
                const QString oldPath = stagingDir + "/" + oldFolder;
                const QString newPath = stagingDir + "/" + newFolder;
                if (QDir(oldPath).exists()) {
                    if (QDir().rename(oldPath, newPath)) {
                        e.stagingFolder = newFolder;
                    } else {
                        qWarning() << "rename: staging folder rename failed for"
                                   << e.id << oldPath << "->" << newPath
                                   << "; keeping old folder";
                        e.stagingFolder = oldFolder;
                    }
                } else {
                    // Nothing staged yet - just record the name-based folder.
                    e.stagingFolder = newFolder;
                }
            } else {
                e.stagingFolder = oldFolder;
            }
        }
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
        // Separators render full-width in the spanned column 0, so rename edits
        // that cell rather than ColName.
        if (entry.type == EntryType::Separator && idx.column() == ColEnabled)
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
    // drop there as "append to the end of the real mod list". Any other invalid
    // dst aborts.
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

        pushUndoSnapshot();
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
        // (but excluding) the next separator whose level is <= this one's. That
        // carries any nested sub-categories (deeper separators) and their mods as a
        // single block, preserving their relative levels. Reordering a section
        // changes deploy order, so we reset + persist + emit modsChanged.
        const int srcLevel = list.at(srcRaw).separatorLevel;
        int blockLen = 1;
        for (int i = srcRaw + 1; i < list.count(); ++i) {
            const auto& e = list.at(i);
            if (e.type == EntryType::Separator && e.separatorLevel <= srcLevel) break;
            ++blockLen;
        }
        // Destination in raw-index terms, adjusted for the block's removal so the
        // section lands where the user dropped it.
        int destRaw = dstRaw;
        if (destRaw > srcRaw) destRaw -= blockLen;
        if (destRaw < 0) destRaw = 0;

        if (destRaw != srcRaw) pushUndoSnapshot();
        beginResetModel();
        list.moveSection(srcRaw, blockLen, destRaw);
        m_profile->save();
        rebuildVisibleRows();
        endResetModel();
        emit modsChanged();
        return true;
    }

    // Snap off a group-child boundary so dropping a single mod into the MIDDLE of
    // an expanded group lands it after the group, never between two children (which
    // would orphan the trailing children / split the group). Only snap when the
    // drop target is a child of a group we're not moving (i.e. a different group),
    // mirroring the group-parent (~599) and moveSelection (~774) paths.
    bool snapped = false;
    if (dstRaw > 0 && dstRaw < list.count()) {
        const auto& at = list.at(dstRaw);
        if (at.type == EntryType::Mod && !at.parentId.isEmpty()
            && at.parentId != list.at(srcRaw).parentId) {
            while (dstRaw < list.count()
                   && list.at(dstRaw).type == EntryType::Mod
                   && !list.at(dstRaw).parentId.isEmpty()) {
                ++dstRaw;
                snapped = true;
            }
        }
    }

    // QList::move() needs a valid destination index in [0, count-1]. A drop at the
    // end maps to the last real slot. Guard the empty case.
    int moveTo = dstRaw;
    const int ceiling = list.count();
    if (moveTo >= ceiling) moveTo = qMax(0, qMin(moveTo, ceiling - 1));
    if (moveTo == srcRaw) return false; // no-op

    // If the snap pushed the destination past the visible drop row, the raw move
    // no longer corresponds 1:1 to the visible (src->dst) pair, so use a full reset
    // (like the group-parent/separator paths) to keep the row mapping consistent.
    if (snapped) {
        pushUndoSnapshot();
        beginResetModel();
        list.move(srcRaw, moveTo);
        m_profile->save();
        rebuildVisibleRows();
        endResetModel();
        emit modsChanged();
        return true;
    }

    // A group child dragged away can't be expressed as a 1:1 (src->dst) move:
    // normalizeGroups() snaps it back adjacent to its parent, which may reorder
    // beyond the move. Use a full reset (like the snapped/group-parent paths).
    const bool movingChild = !list.at(srcRaw).parentId.isEmpty();
    pushUndoSnapshot();
    if (movingChild) {
        beginResetModel();
        list.move(srcRaw, moveTo);
        list.normalizeGroups();   // snap the child back into its parent's run
        m_profile->save();
        rebuildVisibleRows();
        endResetModel();
        emit modsChanged();
        return true;
    }
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
    // Collect every UNIQUE valid selected row (the index list repeats each row
    // once per column), then encode them ascending and comma-separated so a
    // non-contiguous multi-selection can be dragged together.
    QSet<int> rows;
    for (const auto& idx : indexes) if (idx.isValid()) rows.insert(idx.row());
    QList<int> sorted(rows.begin(), rows.end());
    std::sort(sorted.begin(), sorted.end());
    QStringList parts;
    for (int r : sorted) parts << QString::number(r);
    mime->setData(QString::fromLatin1(kModMime), parts.join(',').toLatin1());
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

    // The payload is a comma-separated list of source VISIBLE rows (ascending).
    QList<int> srcVisibleRows;
    for (const auto& part : data->data(QString::fromLatin1(kModMime)).split(',')) {
        bool ok = false;
        const int r = part.toInt(&ok);
        if (!ok) continue;
        // Never drag the pinned Overwrite row.
        if (rawIndexForRow(r) == -1) continue;
        srcVisibleRows << r;
    }
    if (srcVisibleRows.isEmpty()) return false;

    // Destination VISIBLE insertion row.
    int dstVisible;
    if (row >= 0)            dstVisible = row;
    else if (parent.isValid()) dstVisible = parent.row();
    else                     dstVisible = rowCount();

    // Clamp so nothing lands below the pinned Overwrite. rowCount()-1 is the
    // Overwrite's visible index, which moveRows treats as "end of the mod list".
    if (dstVisible < 0) dstVisible = 0;
    if (dstVisible > rowCount() - 1) dstVisible = rowCount() - 1;

    if (srcVisibleRows.size() <= 1) {
        // Single-row drag: unchanged path. moveRows performs the move, persists,
        // refreshes, and emits modsChanged().
        moveRows({}, srcVisibleRows.first(), 1, {}, dstVisible);
    } else {
        // Multi-row drag: lift all selected units and drop them as one block.
        moveSelection(srcVisibleRows, dstVisible);
    }
    // Return false either way so the view doesn't additionally removeRows.
    return false;
}

bool ModListModel::moveSelection(const QList<int>& srcVisibleRows, int dstVisible) {
    if (!m_profile) return false;
    auto& list = m_profile->modList();

    // 2. Expand each source visible row into the raw UNITS it carries.
    QSet<int> rawSet;
    for (int vr : srcVisibleRows) {
        int srcRaw = rawIndexForRow(vr);
        if (srcRaw < 0) continue; // Overwrite or invalid.
        if (isGroupChild(srcRaw)) continue; // carried by its parent.
        const auto& e = list.at(srcRaw);
        if (e.type == EntryType::Separator) {
            // The separator owns itself + every following entry up to (not
            // including) the next separator whose level <= its own.
            const int srcLevel = e.separatorLevel;
            int end = srcRaw + 1;
            for (; end < list.count(); ++end) {
                const auto& n = list.at(end);
                if (n.type == EntryType::Separator && n.separatorLevel <= srcLevel) break;
            }
            for (int i = srcRaw; i < end; ++i) rawSet.insert(i);
        } else if (isGroupParent(srcRaw)) {
            const int end = srcRaw + 1 + groupChildCount(srcRaw);
            for (int i = srcRaw; i < end; ++i) rawSet.insert(i);
        } else {
            rawSet.insert(srcRaw); // plain Mod.
        }
    }

    // 3. Ordered set of raw indices to lift.
    QList<int> orderedSrcRaws(rawSet.begin(), rawSet.end());
    std::sort(orderedSrcRaws.begin(), orderedSrcRaws.end());
    if (orderedSrcRaws.isEmpty()) return false;

    // 4. Destination raw index. Overwrite (-1) -> append to the end of the list.
    int dstRaw = rawIndexForRow(dstVisible);
    if (dstRaw == -1) dstRaw = list.count();
    else if (dstRaw < 0) return false;

    // 5. Snap off a group-child boundary so a non-moved group isn't split.
    while (dstRaw < list.count()
           && list.at(dstRaw).type == EntryType::Mod
           && !list.at(dstRaw).parentId.isEmpty())
        ++dstRaw;

    // 6. Reorder, persist, refresh. Capture the pre-move order so we can record an
    // undo snapshot only if the reorder actually changes anything.
    const QStringList beforeOrder = list.orderIds();
    beginResetModel();
    bool ok = m_profile->modList().reorder(orderedSrcRaws, dstRaw);
    if (ok) {
        m_undoStack.append(beforeOrder);
        if (m_undoStack.size() > kUndoCap) m_undoStack.removeFirst();
        m_redoStack.clear();
        m_profile->save();
    }
    rebuildVisibleRows();
    endResetModel();
    if (ok) {
        emit modsChanged();
        emit undoRedoStateChanged(canUndo(), canRedo());
    }
    return ok;
}

// --- Reorder undo/redo -------------------------------------------------------

void ModListModel::pushUndoSnapshot() {
    if (!m_profile) return;
    m_undoStack.append(m_profile->modList().orderIds());
    if (m_undoStack.size() > kUndoCap) m_undoStack.removeFirst();
    m_redoStack.clear();
    emit undoRedoStateChanged(canUndo(), canRedo());
}

bool ModListModel::applyOrder(const QStringList& ids) {
    if (!m_profile) return false;
    beginResetModel();
    bool ok = m_profile->modList().setOrder(ids);
    if (ok) m_profile->save();
    rebuildVisibleRows();
    endResetModel();
    if (ok) emit modsChanged();
    return ok;
}

bool ModListModel::undoOrder() {
    if (!m_profile || m_undoStack.isEmpty()) return false;
    // Snapshot the current order onto redo, then restore the previous one.
    const QStringList current = m_profile->modList().orderIds();
    const QStringList target = m_undoStack.takeLast();
    m_redoStack.append(current);
    if (m_redoStack.size() > kUndoCap) m_redoStack.removeFirst();
    bool ok = applyOrder(target);
    emit undoRedoStateChanged(canUndo(), canRedo());
    return ok;
}

bool ModListModel::redoOrder() {
    if (!m_profile || m_redoStack.isEmpty()) return false;
    const QStringList current = m_profile->modList().orderIds();
    const QStringList target = m_redoStack.takeLast();
    m_undoStack.append(current);
    if (m_undoStack.size() > kUndoCap) m_undoStack.removeFirst();
    bool ok = applyOrder(target);
    emit undoRedoStateChanged(canUndo(), canRedo());
    return ok;
}

void ModListModel::clearUndoRedo() {
    const bool had = !m_undoStack.isEmpty() || !m_redoStack.isEmpty();
    m_undoStack.clear();
    m_redoStack.clear();
    if (had) emit undoRedoStateChanged(false, false);
}

} // namespace solero
