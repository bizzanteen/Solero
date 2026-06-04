#include "ModListView.h"
#include "ModListModel.h"
#include "SeparatorDialog.h"
#include "install/DependencyChecker.h"
#include "core/AppConfig.h"
#include <QMenu>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMouseEvent>
#include <QInputDialog>
#include <QUuid>
#include <QItemSelectionModel>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QCheckBox>
#include <QDir>

namespace solero {

ModListView::ModListView(QWidget* parent) : QTreeView(parent) {
    m_model = new ModListModel(this);
    setModel(m_model);
    connect(m_model, &ModListModel::modsChanged, this, &ModListView::modsChanged);
    setRootIsDecorated(false);
    setIndentation(0); // remove the empty tree-indent column before the checkbox
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionMode(QAbstractItemView::ExtendedSelection); // Ctrl+click multi-select
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setAlternatingRowColors(true);
    // Name stretches to fill slack; all other columns are user-resizable.
    auto* hdr = header();
    hdr->setSectionResizeMode(ModListModel::ColEnabled,  QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColPriority, QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColName,     QHeaderView::Stretch);
    hdr->setSectionResizeMode(ModListModel::ColVersion,  QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColFlags,    QHeaderView::Interactive);
    hdr->setStretchLastSection(false);
    hdr->resizeSection(ModListModel::ColEnabled,  28);
    hdr->resizeSection(ModListModel::ColPriority, 40);
    hdr->resizeSection(ModListModel::ColVersion,  80);
    hdr->resizeSection(ModListModel::ColFlags,    60);
    setSortingEnabled(true);
    sortByColumn(ModListModel::ColPriority, Qt::AscendingOrder);

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
    // Single-click directly on a separator's collapse arrow toggles it.
    auto idx = indexAt(event->pos());
    if (idx.isValid()) {
        const auto* entry = m_model->entryAt(idx.row());
        if (entry && entry->type == EntryType::Separator) {
            // The arrow glyph is drawn at the left edge of the Name column cell.
            QModelIndex nameIdx = m_model->index(idx.row(), ModListModel::ColName);
            QRect nameRect = visualRect(nameIdx);
            int dx = event->pos().x() - nameRect.left();
            if (dx >= 0 && dx < 22) {
                m_model->toggleCollapse(idx.row());
                return; // consume - don't start a drag/selection
            }
        }
    }
    QTreeView::mousePressEvent(event);
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
        menu.addAction("Open Overwrite Folder", []{ /* Stage 2 */ });
    } else if (entry->type == EntryType::Separator) {
        menu.addAction("Edit Separator", [this, row = idx.row()]{ onEditSeparator(row); });
        menu.addAction(entry->collapsed ? "Expand" : "Collapse",
                       [this, row = idx.row()]{ m_model->toggleCollapse(row); });
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
        menu.addAction("Delete Mod...", [this]{ deleteSelectedMods(); });
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
    sep.color = "#555555";

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

    emit modsChanged();

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
    }
    m_model->profile()->save();
    m_model->rebuild();
    emit modsChanged();
}

} // namespace solero
