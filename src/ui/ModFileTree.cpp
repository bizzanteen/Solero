#include "ModFileTree.h"
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

static constexpr int RoleFullPath = Qt::UserRole;
static constexpr int RoleRelPath  = Qt::UserRole + 1;
static constexpr int RoleIsDir    = Qt::UserRole + 2; // true on folder rows
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
}

void ModFileTree::buildTree(const QString& rootDir,
                            const std::function<void(QTreeWidgetItem*, const QString&)>& decorate) {
    clear();
    // Disable sorting while building so inserts stay O(n) and the existing
    // insertion/nesting logic isn't disturbed; re-enable after so the user's
    // chosen sort re-applies once.
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
                auto* dirItem = parent
                    ? new QTreeWidgetItem(parent, {parts[i], "", ""})
                    : new QTreeWidgetItem(this, {parts[i], "", ""});
                dirItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
                dirItem->setExpanded(true);
                // Record the folder's relative path so the context menu can
                // rename/delete it. RoleFullPath is left unset so folders stay
                // out of drag payloads (only files are draggable/droppable).
                dirItem->setData(0, RoleRelPath, accumulated);
                dirItem->setData(0, RoleIsDir, true);
                dirItems[accumulated] = dirItem;
                parent = dirItem;
            } else {
                parent = dirItems[accumulated];
            }
        }

        QString filename = parts.last();
        QString type = QFileInfo(filename).suffix().toLower();
        auto* item = parent
            ? new QTreeWidgetItem(parent, {filename, type, ""})
            : new QTreeWidgetItem(this, {filename, type, ""});
        item->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        item->setData(0, RoleFullPath, fullPath);
        item->setData(0, RoleRelPath,  relPath);
        decorate(item, relPath);
    }
    setSortingEnabled(wasSorting);
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
                              const QColor& accent) {
    m_stagingRoot.clear(); // game-dir mode: no drops
    m_modId.clear();
    m_hiddenRelPaths.clear();
    setAcceptDrops(false);
    buildTree(gameDir, [&](QTreeWidgetItem* item, const QString& relPath) {
        auto owner = ownerByRelPath.value(relPath);
        if (!owner.isEmpty()) {
            item->setText(2, "from: " + owner);
            item->setForeground(0, accent);
            item->setForeground(2, accent);
            QFont f = item->font(0); f.setBold(true); item->setFont(0, f);
        }
    });
}

void ModFileTree::setFilter(const QString& text) {
    const QString t = text.trimmed();
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
    // These edits apply to a real mod's staging (not the game-dir view, and not
    // the synthetic "__overwrite__"/etc. pseudo-mods whose ids start with "__").
    if (m_modId.isEmpty() || m_modId.startsWith("__")) return;
    QTreeWidgetItem* item = itemAt(e->pos());
    if (!item) return;
    const QString relPath = item->data(0, RoleRelPath).toString();
    if (relPath.isEmpty()) return; // a row with no path we can act on
    const bool isFolder = item->data(0, RoleIsDir).toBool();
    const QString name  = relPath.section('/', -1); // basename within its parent

    QMenu menu(this);
    if (!isFolder) {
        const bool hidden = m_hiddenRelPaths.contains(relPath);
        menu.addAction(hidden ? QStringLiteral("Unhide file")
                              : QStringLiteral("Hide file in this mod"),
                       this, [this, relPath, hidden]{
            emit hideToggled(m_modId, relPath, !hidden);
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
    menu.addAction(QStringLiteral("Delete"), this,
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
