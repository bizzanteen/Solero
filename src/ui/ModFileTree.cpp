#include "ModFileTree.h"
#include <QHeaderView>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QMap>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <functional>

namespace solero {

static constexpr int RoleFullPath = Qt::UserRole;
static constexpr int RoleRelPath  = Qt::UserRole + 1;
static const char* kMime = "application/x-solero-file";

ModFileTree::ModFileTree(QWidget* parent) : QTreeWidget(parent) {
    setHeaderLabels({"File", "Status"});
    header()->setSectionResizeMode(0, QHeaderView::Interactive);
    header()->setSectionResizeMode(1, QHeaderView::Interactive);
    header()->setStretchLastSection(true);
    setColumnWidth(0, 320);
    setColumnWidth(1, 160);
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
    QMap<QString, QTreeWidgetItem*> dirItems;
    QDirIterator it(rootDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
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
                QString folderLabel = QStringLiteral("\xF0\x9F\x93\x81 ") + parts[i]; // 📁
                auto* dirItem = parent
                    ? new QTreeWidgetItem(parent, {folderLabel, ""})
                    : new QTreeWidgetItem(this, {folderLabel, ""});
                dirItem->setExpanded(true);
                dirItems[accumulated] = dirItem;
                parent = dirItem;
            } else {
                parent = dirItems[accumulated];
            }
        }

        QString filename = parts.last();
        auto* item = parent
            ? new QTreeWidgetItem(parent, {filename, ""})
            : new QTreeWidgetItem(this, {filename, ""});
        item->setData(0, RoleFullPath, fullPath);
        item->setData(0, RoleRelPath,  relPath);
        decorate(item, relPath);
    }
}

void ModFileTree::showModFiles(const QString& stagingRoot,
                               const QString& modId,
                               const ConflictIndex& conflicts,
                               const QSet<QString>& editedRelPaths,
                               const QColor& accent) {
    m_stagingRoot = stagingRoot;
    buildTree(stagingRoot, [&](QTreeWidgetItem* item, const QString& relPath) {
        QString status;
        QColor color;
        if (conflicts.hasConflict(relPath)) {
            if (conflicts.winnerOf(relPath) == modId) {
                status = QString("beats %1 mod(s)").arg(conflicts.losersOf(relPath).size());
                color  = QColor("#27ae60");
            } else {
                status = "overwritten by: " + conflicts.winnerOf(relPath);
                color  = QColor("#c0392b");
            }
        }
        bool edited = editedRelPaths.contains(relPath);
        if (edited)
            status = status.isEmpty() ? QStringLiteral("\xE2\x9C\x8E edited")
                                      : QStringLiteral("\xE2\x9C\x8E edited \xE2\x80\xA2 ") + status;
        item->setText(1, status);
        if (color.isValid()) item->setForeground(1, color);
        if (edited) {
            QFont f = item->font(0); f.setItalic(true); item->setFont(0, f);
            item->setForeground(1, QColor("#e67e22"));
        }
        Q_UNUSED(accent);
    });
}

void ModFileTree::showGameDir(const QString& gameDir,
                              const QHash<QString, QString>& ownerByRelPath,
                              const QColor& accent) {
    m_stagingRoot.clear(); // game-dir mode: no drops
    setAcceptDrops(false);
    buildTree(gameDir, [&](QTreeWidgetItem* item, const QString& relPath) {
        auto owner = ownerByRelPath.value(relPath);
        if (!owner.isEmpty()) {
            item->setText(1, "from: " + owner);
            item->setForeground(0, accent);
            item->setForeground(1, accent);
            QFont f = item->font(0); f.setBold(true); item->setFont(0, f);
        }
    });
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

} // namespace solero
