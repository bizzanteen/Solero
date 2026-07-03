#include "VersionDelegate.h"
#include "ModListModel.h"
#include <QComboBox>

namespace solero {

VersionDelegate::VersionDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QWidget* VersionDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem&,
                                       const QModelIndex& index) const {
    const QStringList versions = index.data(ModListModel::VariantListRole).toStringList();
    if (versions.size() < 2) return nullptr; // single-version -> not editable
    auto* combo = new QComboBox(parent);
    combo->addItems(versions);
    connect(combo, QOverload<int>::of(&QComboBox::activated),
            this, &VersionDelegate::commitAndCloseEditor);
    return combo;
}

void VersionDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;
    const int idx = index.data(ModListModel::VariantIndexRole).toInt();
    if (idx >= 0 && idx < combo->count()) combo->setCurrentIndex(idx);
}

void VersionDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                   const QModelIndex& index) const {
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;
    model->setData(index, combo->currentIndex(), ModListModel::VariantIndexRole);
}

void VersionDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                                           const QModelIndex&) const {
    editor->setGeometry(option.rect);
}

void VersionDelegate::commitAndCloseEditor() {
    auto* combo = qobject_cast<QComboBox*>(sender());
    if (!combo) return;
    emit commitData(combo);
    emit closeEditor(combo);
}

} // namespace solero
