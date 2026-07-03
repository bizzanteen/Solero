#pragma once
#include <QStyledItemDelegate>

namespace solero {

// Item delegate for the mod list's Version column. For multi-variant (Keep Both)
// mods it edits with a QComboBox of the variant versions; picking one writes the
// chosen index back through ModListModel::VariantIndexRole. Single-version rows
// produce no editor (createEditor returns nullptr).
class VersionDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit VersionDelegate(QObject* parent = nullptr);

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override;

private slots:
    // Commit + close as soon as the user activates a combo entry (single-click UX).
    void commitAndCloseEditor();
};

} // namespace solero
