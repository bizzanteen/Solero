#include "ModFileTree.h"
#include "ElideDelegate.h"
#include <QHeaderView>
#include <QStyle>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QMap>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPalette>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <functional>

namespace solero {

static constexpr int RoleFullPath   = Qt::UserRole;
static constexpr int RoleRelPath    = Qt::UserRole + 1;
static constexpr int RoleIsDir      = Qt::UserRole + 2; // true on folder rows
static constexpr int RoleUnpopulated = Qt::UserRole + 3; // lazy folder not yet filled
static constexpr int RoleDirPath     = Qt::UserRole + 4; // on-disk path of a folder row
static const char* kMime = "application/x-solero-file";

ModFileTree::ModFileTree(QWidget* parent) : QTreeWidget(parent) {
    setHeaderLabels({"File", "Type", "Status"});
    header()->setSectionResizeMode(0, QHeaderView::Interactive);
    header()->setSectionResizeMode(1, QHeaderView::Interactive);
    header()->setSectionResizeMode(2, QHeaderView::Interactive);
    header()->setStretchLastSection(true);
    header()->setSectionsClickable(true);
    setColumnWidth(0, 320);
    setColumnWidth(1, 70);
    setColumnWidth(2, 160);
    // Char-level elision for long file/folder names ("SomeLongTextu…", not word-cut).
    setItemDelegate(new ElideRightDelegate(this));
    setTextElideMode(Qt::ElideRight);
    setWordWrap(false);
    setSortingEnabled(true);
    sortByColumn(0, Qt::AscendingOrder);
    setRootIsDecorated(true);
    setAlternatingRowColors(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(true);

    connect(this, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
        if (!item) return;
        QString full = item->data(0, RoleFullPath).toString();
        if (!full.isEmpty()) emit fileActivated(full);
    });
    connect(this, &QTreeWidget::itemExpanded, this, &ModFileTree::onItemExpanded);
}

QTreeWidgetItem* ModFileTree::makeDirItem(QTreeWidgetItem* parent, const QString& fullPath,
                                          const QString& relPath, const QString& name) {
    auto* dirItem = parent ? new QTreeWidgetItem(parent, {name, "", ""})
                           : new QTreeWidgetItem(this, {name, "", ""});
    dirItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
    // Record the folder's relative path so the context menu can rename/delete it.
    // RoleFullPath holds the on-disk dir path (used to populate children lazily);
    // it is left unset for drag payloads because only file rows are draggable.
    dirItem->setData(0, RoleRelPath, relPath);
    dirItem->setData(0, RoleIsDir, true);
    if (!fullPath.isEmpty()) dirItem->setData(0, RoleDirPath, fullPath); // dir disk path
    return dirItem;
}

QTreeWidgetItem* ModFileTree::makeFileItem(QTreeWidgetItem* parent, const QString& fullPath,
                                           const QString& relPath, const QString& filename) {
    QString type = QFileInfo(filename).suffix().toLower();
    auto* item = parent ? new QTreeWidgetItem(parent, {filename, type, ""})
                        : new QTreeWidgetItem(this, {filename, type, ""});
    item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
    item->setData(0, RoleFullPath, fullPath);
    item->setData(0, RoleRelPath,  relPath);
    return item;
}

void ModFileTree::buildTree(const QString& rootDir,
                            const std::function<void(QTreeWidgetItem*, const QString&)>& decorate) {
    m_lazy = false;
    m_lazyRoot.clear();
    m_decorate = {};
    clear();
    // Disable view updates + sorting while building so inserts stay O(n) and the
    // view doesn't relayout/repaint per item; re-enable after so the user's
    // chosen sort re-applies once.
    setUpdatesEnabled(false);
    const bool wasSorting = isSortingEnabled();
    setSortingEnabled(false);
    QMap<QString, QTreeWidgetItem*> dirItems;
    QDirIterator it(rootDir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        QString fullPath = it.next();
        QString relPath  = fullPath.mid(rootDir.length() + 1);
        if (relPath.startsWith(".solero")) continue;
        QStringList parts = relPath.split('/');

        QTreeWidgetItem* parent = nullptr;
        QString accumulated;
        for (int i = 0; i < parts.size() - 1; ++i) {
            accumulated += (i > 0 ? "/" : "") + parts[i];
            if (!dirItems.contains(accumulated)) {
                auto* dirItem = makeDirItem(parent, QString(), accumulated, parts[i]);
                dirItem->setExpanded(true);
                dirItems[accumulated] = dirItem;
                parent = dirItem;
            } else {
                parent = dirItems[accumulated];
            }
        }

        auto* item = makeFileItem(parent, fullPath, relPath, parts.last());
        decorate(item, relPath);
    }
    setSortingEnabled(wasSorting);
    setUpdatesEnabled(true);
}

void ModFileTree::buildTreeLazy(const QString& rootDir, const Decorator& decorate) {
    m_lazy = true;
    m_lazyRoot = rootDir;
    m_decorate = decorate;
    clear();
    setUpdatesEnabled(false);
    const bool wasSorting = isSortingEnabled();
    setSortingEnabled(false);
    // Only the top level is materialized; each folder is left collapsed with a
    // placeholder child so QTreeWidget shows an expansion arrow without us
    // creating thousands of rows up front. populateChildren() fills a folder the
    // first time it is expanded.
    populateChildren(nullptr);
    setSortingEnabled(wasSorting);
    setUpdatesEnabled(true);
}

void ModFileTree::populateChildren(QTreeWidgetItem* dir) {
    // Resolve the on-disk path and the rel-prefix for the level we're filling.
    QString dirFull = dir ? dir->data(0, RoleDirPath).toString() : m_lazyRoot;
    QString relPrefix = dir ? (dir->data(0, RoleRelPath).toString() + "/") : QString();
    if (dirFull.isEmpty()) return;

    const bool wasSorting = isSortingEnabled();
    setSortingEnabled(false);
    QDir d(dirFull);
    // Directories first, then files - single non-recursive listing of this level.
    const auto entries = d.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                         QDir::DirsFirst | QDir::Name);
    for (const QFileInfo& fi : entries) {
        const QString name = fi.fileName();
        if (name.startsWith(".solero")) continue;
        const QString rel = relPrefix + name;
        if (fi.isDir()) {
            auto* sub = makeDirItem(dir, fi.absoluteFilePath(), rel, name);
            // Placeholder so the arrow appears; replaced on first expand.
            sub->setData(0, RoleUnpopulated, true);
            new QTreeWidgetItem(sub); // dummy child
        } else {
            auto* item = makeFileItem(dir, fi.absoluteFilePath(), rel, name);
            if (m_decorate) m_decorate(item, rel);
        }
    }
    setSortingEnabled(wasSorting);
}

void ModFileTree::onItemExpanded(QTreeWidgetItem* item) {
    if (!item || !m_lazy) return;
    if (!item->data(0, RoleUnpopulated).toBool()) return;
    item->setData(0, RoleUnpopulated, false);
    setUpdatesEnabled(false);
    // Drop the placeholder child, then fill the real entries for this level.
    while (item->childCount() > 0) delete item->takeChild(0);
    populateChildren(item);
    setUpdatesEnabled(true);
}

void ModFileTree::materializeAll() {
    if (!m_lazy) return;
    // Force-populate every still-collapsed folder (used by filter / expand-all,
    // which need the whole tree present). Iterative so deep trees don't recurse
    // the C++ stack; populateChildren appends to children we then revisit.
    std::function<void(QTreeWidgetItem*)> fill = [&](QTreeWidgetItem* it) {
        if (it->data(0, RoleUnpopulated).toBool()) {
            it->setData(0, RoleUnpopulated, false);
            while (it->childCount() > 0) delete it->takeChild(0);
            populateChildren(it);
        }
        for (int i = 0; i < it->childCount(); ++i) fill(it->child(i));
    };
    setUpdatesEnabled(false);
    for (int i = 0; i < topLevelItemCount(); ++i) fill(topLevelItem(i));
    m_lazy = false; // fully materialized now; no further on-demand work needed
    setUpdatesEnabled(true);
}

void ModFileTree::expandTree() {
    if (m_lazy) {
        // Expanding a lazy game tree fully would materialize every file (the slow
        // path we are avoiding). Only open the top level the user can already see;
        // deeper levels populate on demand as they are expanded.
        setUpdatesEnabled(false);
        for (int i = 0; i < topLevelItemCount(); ++i) topLevelItem(i)->setExpanded(true);
        setUpdatesEnabled(true);
        return;
    }
    expandAll();
}

void ModFileTree::collapseTree() {
    if (m_lazy) {
        setUpdatesEnabled(false);
        for (int i = 0; i < topLevelItemCount(); ++i) topLevelItem(i)->setExpanded(false);
        setUpdatesEnabled(true);
        return;
    }
    collapseAll();
}

void ModFileTree::showModFiles(const QString& stagingRoot,
                               const QString& modId,
                               const ConflictIndex& conflicts,
                               const QSet<QString>& editedRelPaths,
                               const QColor& accent,
                               const std::function<QString(const QString&)>& nameOf,
                               const QSet<QString>& hiddenRelPaths) {
    m_stagingRoot = stagingRoot;
    m_modId = modId;
    m_hiddenRelPaths = hiddenRelPaths;
    auto disp = [&](const QString& id){ return (nameOf && !id.isEmpty()) ? nameOf(id) : id; };
    buildTree(stagingRoot, [&](QTreeWidgetItem* item, const QString& relPath) {
        // A hidden file is not provided on deploy; show it struck-through and
        // dimmed, taking visual precedence over conflict status.
        if (hiddenRelPaths.contains(relPath)) {
            item->setText(2, QStringLiteral("hidden"));
            QFont f = item->font(0); f.setStrikeOut(true); item->setFont(0, f);
            QColor dim = palette().color(QPalette::Disabled, QPalette::Text);
            item->setForeground(0, dim);
            item->setForeground(2, dim);
            return;
        }
        QString status;
        QColor color;
        // The conflict index is keyed by the canonical case-folded path, so look up
        // the lowercased relPath (Wine/Proton collapses case-variants on disk).
        const QString cKey = relPath.toLower();
        if (conflicts.hasConflict(cKey)) {
            if (conflicts.winnerOf(cKey) == modId) {
                auto losers = conflicts.losersOf(cKey);
                QString first = losers.isEmpty() ? QString() : disp(*losers.begin());
                status = losers.size() > 1
                    ? QString("Overwrites %1 +%2 more").arg(first).arg(losers.size() - 1)
                    : QString("Overwrites %1").arg(first);
                color  = QColor("#27ae60");
            } else {
                status = "Overwritten by " + disp(conflicts.winnerOf(cKey));
                color  = QColor("#c0392b");
            }
        }
        bool edited = editedRelPaths.contains(relPath);
        if (edited)
            status = status.isEmpty() ? QStringLiteral("edited")
                                      : QStringLiteral("edited ") + QChar(0x2022) + QChar(' ') + status;
        item->setText(2, status);
        if (color.isValid()) item->setForeground(2, color);
        if (edited) {
            QFont f = item->font(0); f.setItalic(true); item->setFont(0, f);
            item->setForeground(2, QColor("#e67e22"));
        }
        Q_UNUSED(accent);
    });
}

void ModFileTree::showGameDir(const QString& gameDir,
                              const QHash<QString, QString>& ownerByRelPath,
                              const QHash<QString, QString>& ownerModIdByRel,
                              const QSet<QString>& hiddenByRel,
                              const QColor& accent) {
    m_stagingRoot.clear(); // game-dir mode: no drops
    m_modId.clear();
    m_hiddenRelPaths.clear();
    m_gameOwnerModId = ownerModIdByRel;
    m_gameHidden = hiddenByRel;
    m_gameOwnerName = ownerByRelPath;
    m_accent = accent;
    setAcceptDrops(false);
    // The game Data folder can hold thousands of files. Build it eagerly (with
    // view updates suspended during the build - see buildTree) so the whole tree
    // is present for searching; a lazy tree made the toggle fast but forced a
    // full synchronous materialize the moment the user typed in the filter, which
    // froze the UI. ownerByRelPath is captured by value for the decorator.
    buildTree(gameDir, [ownerByRelPath, accent, this]
                  (QTreeWidgetItem* item, const QString& relPath) {
        // A hidden file takes visual precedence (matching showModFiles): show it
        // struck-through and dimmed instead of the owner decoration.
        if (m_gameHidden.contains(relPath)) {
            applyHiddenStyle(item, true);
            return;
        }
        auto owner = ownerByRelPath.value(relPath);
        if (!owner.isEmpty()) {
            item->setText(2, "from: " + owner);
            item->setForeground(0, accent);
            item->setForeground(2, accent);
            QFont f = item->font(0); f.setBold(true); item->setFont(0, f);
        }
    });
}

void ModFileTree::applyHiddenStyle(QTreeWidgetItem* item, bool hidden) {
    if (hidden) {
        item->setText(2, QStringLiteral("hidden"));
        QFont f = item->font(0); f.setStrikeOut(true); item->setFont(0, f);
        QColor dim = palette().color(QPalette::Disabled, QPalette::Text);
        item->setForeground(0, dim);
        item->setForeground(2, dim);
        return;
    }
    // Unhide: drop the strikeout, then restore the row's normal appearance.
    QFont f = item->font(0); f.setStrikeOut(false); item->setFont(0, f);
    if (m_modId.isEmpty()) {
        // Game-dir mode: re-apply the owner decoration if this file has a known
        // owner, exactly as showGameDir's build decorator does.
        const QString relPath = item->data(0, RoleRelPath).toString();
        const QString owner = m_gameOwnerName.value(relPath);
        if (!owner.isEmpty()) {
            item->setText(2, "from: " + owner);
            item->setForeground(0, m_accent);
            item->setForeground(2, m_accent);
            QFont bf = item->font(0); bf.setBold(true); item->setFont(0, bf);
            return;
        }
    }
    // Single-mod mode (or ownerless game-dir row): clear the status and reset the
    // foregrounds to default. In single-mod mode the cheap rebuild restores the
    // full conflict/edited decoration.
    item->setText(2, QString());
    item->setForeground(0, {});
    item->setForeground(2, {});
}

void ModFileTree::setFilter(const QString& text) {
    const QString t = text.trimmed();
    // An empty filter on a lazy tree is a no-op: nothing is hidden and we must
    // not force the whole (huge) game tree to materialize just to clear a search.
    if (t.isEmpty() && m_lazy) return;
    // A real search needs every row present to match against, so realize the
    // lazy tree once before walking it.
    if (!t.isEmpty()) materializeAll();
    std::function<bool(QTreeWidgetItem*)> apply = [&](QTreeWidgetItem* it) -> bool {
        bool selfMatch = t.isEmpty() || it->text(0).contains(t, Qt::CaseInsensitive);
        bool childMatch = false;
        for (int i = 0; i < it->childCount(); ++i)
            childMatch = apply(it->child(i)) || childMatch;
        bool visible = selfMatch || childMatch;
        it->setHidden(!visible);
        if (childMatch && !t.isEmpty()) it->setExpanded(true);
        return visible;
    };
    for (int i = 0; i < topLevelItemCount(); ++i) apply(topLevelItem(i));
}

QStringList ModFileTree::mimeTypes() const {
    return { QString::fromLatin1(kMime) };
}

QMimeData* ModFileTree::mimeData(const QList<QTreeWidgetItem*>& items) const {
    auto* mime = new QMimeData;
    QStringList lines;
    for (auto* item : items) {
        QString full = item->data(0, RoleFullPath).toString();
        QString rel  = item->data(0, RoleRelPath).toString();
        if (!full.isEmpty()) lines << (full + '\t' + rel);
    }
    mime->setData(QString::fromLatin1(kMime), lines.join('\n').toUtf8());
    return mime;
}

void ModFileTree::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(QString::fromLatin1(kMime)) && !m_stagingRoot.isEmpty())
        e->acceptProposedAction();
    else
        e->ignore();
}

void ModFileTree::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat(QString::fromLatin1(kMime)) && !m_stagingRoot.isEmpty())
        e->acceptProposedAction();
    else
        e->ignore();
}

void ModFileTree::dropEvent(QDropEvent* e) {
    if (m_stagingRoot.isEmpty()) { e->ignore(); return; }
    QByteArray raw = e->mimeData()->data(QString::fromLatin1(kMime));
    if (raw.isEmpty()) { e->ignore(); return; }

    bool any = false;
    for (const auto& line : QString::fromUtf8(raw).split('\n', Qt::SkipEmptyParts)) {
        auto fields = line.split('\t');
        if (fields.size() != 2) continue;
        QString srcFull = fields[0];
        QString relPath = fields[1];
        QString dstFull = m_stagingRoot + "/" + relPath;
        if (QFileInfo(srcFull).absoluteFilePath() == QFileInfo(dstFull).absoluteFilePath())
            continue; // dropping onto self
        QDir().mkpath(QFileInfo(dstFull).path());
        QFile::remove(dstFull);
        if (QFile::copy(srcFull, dstFull)) any = true;
    }
    e->acceptProposedAction();
    if (any) emit filesDropped();
}

void ModFileTree::contextMenuEvent(QContextMenuEvent* e) {
    // Game-dir (merged "show all files") mode: m_modId/m_stagingRoot are empty.
    // The mod-editing actions (rename/delete) don't apply, but a deployed file's
    // owner is known via m_gameOwnerModId, so offer the same Hide/Unhide action,
    // emitting hideToggled against that owner. Loose/Overwrite/ownerless rows and
    // folder rows get nothing.
    if (m_modId.isEmpty()) {
        QTreeWidgetItem* item = itemAt(e->pos());
        if (!item) return;
        if (item->data(0, RoleIsDir).toBool()) return; // folders: no hide
        const QString relPath = item->data(0, RoleRelPath).toString();
        if (relPath.isEmpty()) return;
        const QString ownerModId = m_gameOwnerModId.value(relPath);
        // No real owner (loose / Overwrite / pseudo-mod) -> no hide action.
        if (ownerModId.isEmpty() || ownerModId.startsWith("__")) return;
        const bool hidden = m_gameHidden.contains(relPath);
        QMenu menu(this);
        menu.addAction(hidden ? QStringLiteral("Unhide File in This Mod")
                              : QStringLiteral("Hide File in This Mod"),
                       this, [this, ownerModId, relPath, hidden, item]{
            const bool nowHidden = !hidden;
            emit hideToggled(ownerModId, relPath, nowHidden);
            // Instant in-place feedback: update our local set and restyle the row
            // so the merged view doesn't need an expensive full-tree rebuild.
            if (nowHidden) m_gameHidden.insert(relPath);
            else           m_gameHidden.remove(relPath);
            applyHiddenStyle(item, nowHidden);
        });
        menu.exec(e->globalPos());
        return;
    }

    // These edits apply to a real mod's staging (not the synthetic
    // "__overwrite__"/etc. pseudo-mods whose ids start with "__").
    if (m_modId.startsWith("__")) return;
    QTreeWidgetItem* item = itemAt(e->pos());
    if (!item) return;
    const QString relPath = item->data(0, RoleRelPath).toString();
    if (relPath.isEmpty()) return; // a row with no path we can act on
    const bool isFolder = item->data(0, RoleIsDir).toBool();
    const QString name  = relPath.section('/', -1); // basename within its parent

    QMenu menu(this);
    if (!isFolder) {
        const bool hidden = m_hiddenRelPaths.contains(relPath);
        menu.addAction(hidden ? QStringLiteral("Unhide File in This Mod")
                              : QStringLiteral("Hide File in This Mod"),
                       this, [this, relPath, hidden, item]{
            const bool nowHidden = !hidden;
            emit hideToggled(m_modId, relPath, nowHidden);
            // Instant in-place feedback before DataTab's cheap rebuild lands.
            if (nowHidden) m_hiddenRelPaths.insert(relPath);
            else           m_hiddenRelPaths.remove(relPath);
            applyHiddenStyle(item, nowHidden);
        });
        menu.addSeparator();
    }

    // Rename… - basename only, within the same parent dir.
    menu.addAction(QStringLiteral("Rename") + QChar(0x2026), this,
                   [this, relPath, name, isFolder]{
        bool ok = false;
        const QString input = QInputDialog::getText(
            this, QStringLiteral("Rename"), QStringLiteral("New name:"),
            QLineEdit::Normal, name, &ok);
        if (!ok) return;
        const QString newName = input.trimmed();
        if (newName.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Rename"),
                                 QStringLiteral("The name cannot be empty."));
            return;
        }
        if (newName.contains('/') || newName.contains('\\')) {
            QMessageBox::warning(this, QStringLiteral("Rename"),
                QStringLiteral("The name cannot contain path separators."));
            return;
        }
        if (newName == name) return; // unchanged
        // Reject a collision with an existing sibling in the same parent dir.
        const int slash = relPath.lastIndexOf('/');
        const QString parentRel = slash >= 0 ? relPath.left(slash + 1) : QString();
        if (QFileInfo::exists(m_stagingRoot + "/" + parentRel + newName)) {
            QMessageBox::warning(this, QStringLiteral("Rename"),
                QStringLiteral("'%1' already exists here.").arg(newName));
            return;
        }
        emit renameRequested(m_modId, relPath, newName, isFolder);
    });

    // Delete - confirm first; defaults to No.
    menu.addAction(QStringLiteral("Delete") + QChar(0x2026), this,
                   [this, relPath, name, isFolder]{
        const QString msg = isFolder
            ? QStringLiteral("Are you sure you want to delete '%1'?\n"
                             "This deletes the folder and its contents from the "
                             "mod's files.").arg(name)
            : QStringLiteral("Are you sure you want to delete '%1'?\n"
                             "This removes it from the mod's files.").arg(name);
        if (QMessageBox::question(this, QStringLiteral("Delete"), msg,
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        emit deleteRequested(m_modId, relPath, isFolder);
    });

    menu.exec(e->globalPos());
}

} // namespace solero
