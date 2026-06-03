#include "ModListView.h"
#include "ModListModel.h"
#include "SeparatorDialog.h"
#include <QMenu>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMouseEvent>

namespace solero {

ModListView::ModListView(QWidget* parent) : QTreeView(parent) {
    m_model = new ModListModel(this);
    setModel(m_model);
    setRootIsDecorated(false);
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionMode(QAbstractItemView::SingleSelection);
    header()->setSectionResizeMode(ModListModel::ColName, QHeaderView::Stretch);
    header()->resizeSection(ModListModel::ColEnabled, 28);
    header()->resizeSection(ModListModel::ColVersion, 80);
    header()->resizeSection(ModListModel::ColFlags, 60);
}

void ModListView::setProfile(Profile* profile) {
    m_model->setProfile(profile);
}

void ModListView::mouseDoubleClickEvent(QMouseEvent* event) {
    auto idx = indexAt(event->pos());
    if (!idx.isValid()) { QTreeView::mouseDoubleClickEvent(event); return; }
    const auto* entry = m_model->entryAt(idx.row());
    if (entry && entry->type == EntryType::Separator) {
        m_model->toggleCollapse(idx.row());
        return;
    }
    QTreeView::mouseDoubleClickEvent(event);
}

void ModListView::contextMenuEvent(QContextMenuEvent* event) {
    auto idx = indexAt(event->pos());
    if (!idx.isValid()) return;
    const auto* entry = m_model->entryAt(idx.row());
    QMenu menu(this);
    if (!entry) {
        // Overwrite row
        menu.addAction("Open Overwrite Folder", []{ /* Stage 2 */ });
    } else if (entry->type == EntryType::Separator) {
        menu.addAction("Edit Separator", [this, row = idx.row()]{ onEditSeparator(row); });
        menu.addAction(entry->collapsed ? "Expand" : "Collapse",
                       [this, row = idx.row()]{ m_model->toggleCollapse(row); });
    } else {
        menu.addAction("Open in File Manager", []{ /* stub */ });
        menu.addAction("Reinstall (FOMOD)", []{ /* Stage 3 */ });
    }
    menu.exec(event->globalPos());
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

} // namespace solero
