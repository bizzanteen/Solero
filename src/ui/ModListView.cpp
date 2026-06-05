#include "ModListView.h"
#include "ModListModel.h"
#include "SeparatorDialog.h"
#include "install/DependencyChecker.h"
#include "core/AppConfig.h"
#include "core/Profile.h"
#include "ui/IconUtil.h"
#include <QMenu>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QInputDialog>
#include <QUuid>
#include <QItemSelectionModel>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QListWidget>
#include <QWidgetAction>
#include <QStyledItemDelegate>
#include <QLineEdit>
#include <QShowEvent>
#include <QTimer>

namespace solero {

namespace {
class RenameDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setEditorData(QWidget* editor, const QModelIndex& idx) const override {
        if (auto* le = qobject_cast<QLineEdit*>(editor)) {
            le->setText(idx.data(Qt::EditRole).toString());
            le->end(false); // cursor at end, nothing selected (instead of select-all)
        } else { QStyledItemDelegate::setEditorData(editor, idx); }
    }
};
} // namespace

ModListView::ModListView(QWidget* parent) : QTreeView(parent) {
    m_model = new ModListModel(this);
    setModel(m_model);
    connect(m_model, &ModListModel::modsChanged, this, &ModListView::modsChanged);
    setRootIsDecorated(false);
    setIndentation(0); // remove the empty tree-indent column before the checkbox
    setDragDropMode(QAbstractItemView::InternalMove);
    // InternalMove normally implies these, but set them explicitly so the reorder
    // drag reliably starts and accepts drops.
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection); // Ctrl+click multi-select
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setAlternatingRowColors(true);
    setIconSize(QSize(20, 20));
    setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    setItemDelegateForColumn(ModListModel::ColName, new RenameDelegate(this));
    // Every column is user-resizable (Interactive). A middle Stretch section makes
    // manual resizes feel inverted, so we instead auto-fit on first show.
    auto* hdr = header();
    hdr->setSectionResizeMode(ModListModel::ColEnabled,  QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColPriority, QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColName,     QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColVersion,  QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColFlags,    QHeaderView::Interactive);
    hdr->setStretchLastSection(false);
    hdr->resizeSection(ModListModel::ColEnabled,  28);
    hdr->resizeSection(ModListModel::ColPriority, 40);
    hdr->resizeSection(ModListModel::ColVersion,  80);
    hdr->resizeSection(ModListModel::ColFlags,    60);
    // The list is manually ordered (drag-reorder); there's no real sort(), so
    // clickable headers would only flip a lying sort indicator. Disable sorting.
    setSortingEnabled(false);
    header()->setSectionsClickable(false);

    connect(selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection&, const QItemSelection&) {
        QStringList ids;
        const auto rows = selectionModel()->selectedRows();
        for (const auto& idx : rows) {
            const auto* entry = m_model->entryAt(idx.row());
            if (!entry)
                ids << "__overwrite__";
            else if (entry->type == EntryType::Separator)
                ids << "__separator__";
            else
                ids << entry->id;
        }
        emit modsSelected(ids);
    });
}

void ModListView::setProfile(Profile* profile) {
    m_model->setProfile(profile);
    if (profile) {
        auto warns = DependencyChecker::check(profile->modList(),
                        AppConfig::instance().stagingDir());
        m_model->setDependencyWarnings(warns);
    }
    applyFilter(); // model rebuilt -> re-apply any active filter
    // Re-fit columns once the profile's data first populates. The viewport-width
    // guard in autoSizeColumns() makes this a no-op if the view isn't shown yet.
    QTimer::singleShot(0, this, [this]{ autoSizeColumns(); });
}

void ModListView::autoSizeColumns() {
    // Fit every column to header + cell contents (leaves sections Interactive).
    header()->resizeSections(QHeaderView::ResizeToContents);
    const int vw = viewport()->width();
    if (vw <= 0) return; // not laid out yet - keep the ResizeToContents result
    int other = 0;
    for (int c = 0; c < m_model->columnCount(); ++c)
        if (c != ModListModel::ColName) other += header()->sectionSize(c);
    constexpr int kMinName = 160;
    const int target = qMax(kMinName, vw - other);
    // Only widen the Name column up to the available slack; if contents are
    // already wider, leave them (a horizontal scrollbar is fine).
    if (target > header()->sectionSize(ModListModel::ColName))
        header()->resizeSection(ModListModel::ColName, target);
}

void ModListView::showEvent(QShowEvent* event) {
    QTreeView::showEvent(event);
    if (!m_didAutoSize) {
        m_didAutoSize = true;
        QTimer::singleShot(0, this, [this]{ autoSizeColumns(); });
    }
}

void ModListView::invalidateModCache(const QString& id) {
    m_model->invalidateModCache(id);
}

void ModListView::setUpdateInfo(const QHash<QString, QPair<QString,QString>>& info) {
    m_model->setUpdateInfo(info);
}

void ModListView::mouseDoubleClickEvent(QMouseEvent* event) {
    auto idx = indexAt(event->pos());
    if (!idx.isValid()) { QTreeView::mouseDoubleClickEvent(event); return; }
    const auto* entry = m_model->entryAt(idx.row());
    if (entry && entry->type == EntryType::Separator) {
        m_model->toggleCollapse(idx.row());
        return;
    }
    // Mod (or Overwrite): activate -> right pane shows its Data.
    emit modActivated(entry ? entry->id : QString("__overwrite__"));
    QTreeView::mouseDoubleClickEvent(event);
}

void ModListView::mousePressEvent(QMouseEvent* event) {
    auto idx = indexAt(event->pos());
    if (idx.isValid()) {
        const auto* entry = m_model->entryAt(idx.row());
        if (entry && entry->type == EntryType::Separator
            && selectionModel()->isSelected(idx)) {
            QModelIndex nameIdx = m_model->index(idx.row(), ModListModel::ColName);
            QRect r = visualRect(nameIdx);
            QRect iconRect(r.left() + 2, r.top(), iconSize().width() + 6, r.height());
            if (iconRect.contains(event->pos())) {
                showIconPicker(idx.row(), event->globalPosition().toPoint());
                return; // consume
            }
        }
    }
    QTreeView::mousePressEvent(event);
}

void ModListView::showIconPicker(int row, const QPoint& gpos) {
    const auto* entry = m_model->entryAt(row);
    if (!entry || entry->type != EntryType::Separator || !m_model->profile()) return;
    QString id = entry->id;
    QMenu menu(this);
    auto* grid = new QListWidget(&menu);
    grid->setViewMode(QListView::IconMode);
    grid->setIconSize(QSize(26,26));
    grid->setGridSize(QSize(38,38));
    grid->setUniformItemSizes(true);
    grid->setMovement(QListView::Static);
    grid->setResizeMode(QListView::Adjust);
    grid->setSpacing(2);
    grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid->setStyleSheet("background:#2b2b2b; border:none;");
    grid->setFrameShape(QFrame::NoFrame);
    // "None" first
    auto* none = new QListWidgetItem(grid); none->setData(Qt::UserRole, QString()); none->setToolTip("None");
    none->setIcon(solero::redCrossIcon(26));
    none->setSizeHint(QSize(34,34));
    for (const QString& f : QDir(":/icons/separators").entryList(QStringList()<<"*.svg", QDir::Files)) {
        QString path = ":/icons/separators/" + f;
        auto* it = new QListWidgetItem(grid);
        it->setIcon(solero::renderSvgIcon(path, Qt::white, 26));
        it->setData(Qt::UserRole, path);
        it->setToolTip(QFileInfo(f).completeBaseName());
        it->setSizeHint(QSize(34,34));
    }
    // ~6 columns
    grid->setFixedSize(6*38 + 16, 5*38 + 8);
    auto* wa = new QWidgetAction(&menu);
    wa->setDefaultWidget(grid);
    menu.addAction(wa);
    connect(grid, &QListWidget::itemClicked, this, [this, id, &menu](QListWidgetItem* it){
        if (auto* e = m_model->profile() ? m_model->profile()->modList().findById(id) : nullptr) {
            ModEntry up = *e; up.icon = it->data(Qt::UserRole).toString();
            m_model->profile()->modList().update(id, up);
            m_model->profile()->save();
            m_model->rebuild();
        }
        menu.close();
    });
    menu.exec(gpos);
}

void ModListView::contextMenuEvent(QContextMenuEvent* event) {
    auto idx = indexAt(event->pos());
    QMenu menu(this);

    if (!idx.isValid()) {
        // Right-click on empty space
        menu.addAction("Add Separator", [this]{ onAddSeparator(); });
        menu.exec(event->globalPos());
        return;
    }

    const auto* entry = m_model->entryAt(idx.row());
    if (!entry) {
        // Overwrite row
        menu.addAction("Open Overwrite Folder", []{
            QString ow = AppConfig::dataRoot() + "/overwrite";
            QDir().mkpath(ow);
            QDesktopServices::openUrl(QUrl::fromLocalFile(ow));
        });
    } else if (entry->type == EntryType::Separator) {
        menu.addAction("Edit Separator", [this, row = idx.row()]{ onEditSeparator(row); });
        menu.addAction(entry->collapsed ? "Expand" : "Collapse",
                       [this, row = idx.row()]{ m_model->toggleCollapse(row); });
        menu.addAction("Delete Separator", [this, row = idx.row()]{ onDeleteSeparator(row); });
        menu.addSeparator();
        menu.addAction("Add Separator Below", [this, row = idx.row()]{ onAddSeparatorAt(row + 1); });
    } else {
        // If the right-clicked mod isn't part of the current selection, make it
        // the sole selection so delete/operations act on what the user clicked.
        if (!selectionModel()->isRowSelected(idx.row(), idx.parent())) {
            selectionModel()->select(
                m_model->index(idx.row(), 0),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        menu.addAction("Open in File Manager", [this, id = entry->id]{
            QString dir = AppConfig::instance().stagingDir() + "/" + id;
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        menu.addAction(entry->hasFomodChoices ? "Reinstall (FOMOD)..." : "Reinstall...",
                       [this, id = entry->id]{ emit reinstallRequested(id); });
        if (!entry->nexusModId.isEmpty()) {
            menu.addAction("Update Mod",
                           [this, id = entry->id]{ emit updateRequested(id); });
            menu.addAction("Endorse on Nexus",
                           [this, id = entry->id]{ emit endorseRequested(id); });
        }
        menu.addAction("Identify on Nexus (MD5)",
                       [this, id = entry->id]{ emit identifyRequested(id); });
        menu.addAction("Delete Mod...", [this]{ deleteSelectedMods(); });
        menu.addAction("Rename", [this, row = idx.row()]{ edit(m_model->index(row, ModListModel::ColName)); });
        menu.addSeparator();
        menu.addAction("Enable selected",  [this]{ setSelectedModsEnabled(true); });
        menu.addAction("Disable selected", [this]{ setSelectedModsEnabled(false); });
        menu.addSeparator();
        menu.addAction("Add Separator Above", [this, row = idx.row()]{ onAddSeparatorAt(row); });
    }
    menu.exec(event->globalPos());
}

void ModListView::onAddSeparator() {
    onAddSeparatorAt(m_model->rowCount() - 1); // before Overwrite
}

void ModListView::onAddSeparatorAt(int visibleRow) {
    if (!m_model->profile()) return;
    bool ok;
    QString name = QInputDialog::getText(this, "New Separator", "Separator name:", QLineEdit::Normal, "New Category", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    ModEntry sep;
    sep.type = EntryType::Separator;
    sep.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sep.name = name.trimmed();
    static const QStringList kPalette = {"#c0392b","#d35400","#f39c12","#27ae60","#16a085","#2980b9","#8e44ad","#7f8c8d","#2c3e50","#c2185b"};
    QString last = AppConfig::instance().lastSeparatorColor();
    QString chosen;
    if (AppConfig::instance().cycleSeparatorColors()) {
        int i = kPalette.indexOf(last);
        chosen = kPalette.at((i + 1) % kPalette.size());  // i==-1 -> index 0
    } else {
        chosen = last.isEmpty() ? kPalette.first() : last;
    }
    sep.color = chosen;
    AppConfig::instance().setLastSeparatorColor(chosen);
    AppConfig::instance().save();

    // Insert at the given visible row position in the raw list
    int rawPos = m_model->rawIndexForRow(visibleRow);
    if (rawPos < 0) rawPos = m_model->profile()->modList().count();

    // Append then move to position
    m_model->profile()->modList().append(sep);
    int newRaw = m_model->profile()->modList().count() - 1;
    if (rawPos < newRaw)
        m_model->profile()->modList().move(newRaw, rawPos);
    m_model->profile()->save();
    m_model->rebuild();

    // Open edit dialog immediately so user can pick colour/icon
    onEditSeparator(m_model->rawToVisible(rawPos));
}

void ModListView::onEditSeparator(int visibleRow) {
    const auto* entry = m_model->entryAt(visibleRow);
    if (!entry || entry->type != EntryType::Separator) return;
    SeparatorDialog dlg(*entry, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_model->profile()->modList().update(entry->id, dlg.result());
        m_model->profile()->save();
        m_model->rebuild();
    }
}

void ModListView::onDeleteSeparator(int visibleRow) {
    const auto* entry = m_model->entryAt(visibleRow);
    if (!entry || entry->type != EntryType::Separator) return;
    QString id = entry->id;
    QString name = entry->name;
    if (QMessageBox::question(this, "Delete Separator",
            QString("Delete separator \"%1\"? (Mods under it are not deleted.)").arg(name))
        != QMessageBox::Yes) return;
    m_model->profile()->modList().remove(id);
    m_model->profile()->save();
    m_model->rebuild();
}

void ModListView::deleteSelectedMods() {
    if (!m_model->profile()) return;

    // Gather the selected mod entries (skip separators / Overwrite).
    QStringList ids, names;
    const auto rows = selectionModel()->selectedRows();
    for (const auto& idx : rows) {
        const auto* entry = m_model->entryAt(idx.row());
        if (!entry || entry->type != EntryType::Mod) continue;
        ids   << entry->id;
        names << entry->name;
    }
    if (ids.isEmpty()) return;

    if (AppConfig::instance().confirmModDeletion()) {
        QString text = QString("Delete %1 mod(s)? This removes them from the list "
                               "and deletes their staged files. This cannot be undone.")
                           .arg(ids.size());
        if (ids.size() <= 5)
            text += "\n\n\xe2\x80\xa2 " + names.join("\n\xe2\x80\xa2 ");
        QMessageBox box(QMessageBox::Question, "Delete Mod", text,
                        QMessageBox::Yes | QMessageBox::No, this);
        auto* dontAsk = new QCheckBox("Don't ask me again", &box);
        box.setCheckBox(dontAsk);
        if (box.exec() != QMessageBox::Yes) return;
        if (dontAsk->isChecked()) {
            AppConfig::instance().setConfirmModDeletion(false);
            AppConfig::instance().save();
        }
    }

    const QString stagingDir = AppConfig::instance().stagingDir();
    for (const QString& id : ids) {
        QDir(stagingDir + "/" + id).removeRecursively();
        m_model->profile()->modList().remove(id);
        m_model->invalidateModCache(id); // its staged files are now gone
    }
    m_model->profile()->save();
    m_model->rebuild();
    emit modsChanged();
}

void ModListView::setFilter(const QString& text) {
    m_filter = text.trimmed();
    applyFilter();
}

void ModListView::applyFilter() {
    // Hide Mod rows whose name doesn't contain the filter text. Separators and
    // the Overwrite row remain visible. rebuild() clears hidden state, so callers
    // that rebuild the model should re-apply the filter afterwards.
    const QModelIndex root;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const auto* entry = m_model->entryAt(row);
        bool hide = false;
        if (entry && entry->type == EntryType::Mod && !m_filter.isEmpty())
            hide = !entry->name.contains(m_filter, Qt::CaseInsensitive);
        setRowHidden(row, root, hide);
    }
}

void ModListView::setSelectedModsEnabled(bool enabled) {
    if (!m_model->profile()) return;
    QStringList ids;
    const auto rows = selectionModel()->selectedRows();
    for (const auto& idx : rows) {
        const auto* entry = m_model->entryAt(idx.row());
        if (entry && entry->type == EntryType::Mod) ids << entry->id;
    }
    if (ids.isEmpty()) return;
    for (const QString& id : ids)
        m_model->profile()->modList().setEnabled(id, enabled);
    m_model->profile()->save();
    m_model->rebuild();
    applyFilter();
    emit modsChanged();
}

void ModListView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        deleteSelectedMods();
        return;
    }
    QTreeView::keyPressEvent(event);
}

} // namespace solero
