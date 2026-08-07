#pragma once
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QApplication>
#include <QStyle>

namespace solero {

// Elides cell text per character ("Some Long Mod Na...") rather than at word
// boundaries. Qt lays item text out through QTextLayout, which wraps at words
// before eliding, and textElideMode alone doesn't change that - so pre-elide with
// QFontMetrics and turn the style's own elision off.
class ElideRightDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* opt, const QModelIndex& idx) const override {
        QStyledItemDelegate::initStyleOption(opt, idx);
        elideRight(opt);
    }

    // Elide opt->text (in place) at character precision to fit the item's text
    // sub-rect, and disable the style's own eliding so it isn't done twice.
    // Exposed so delegates that first mutate the style option (e.g. move an icon)
    // can call it once their layout is settled.
    static void elideRight(QStyleOptionViewItem* opt) {
        if (opt->text.isEmpty()) return;
        const QWidget* w = opt->widget;
        QStyle* style = w ? w->style() : QApplication::style();
        // The real text sub-rect accounts for the checkbox/icon/margins, so we
        // elide against exactly the width the text will be painted into.
        const QRect r = style->subElementRect(QStyle::SE_ItemViewItemText, opt, w);
        if (r.width() <= 0) return; // fall back to the style's own elision
        opt->text = opt->fontMetrics.elidedText(opt->text, Qt::ElideRight, r.width());
        opt->textElideMode = Qt::ElideNone; // already elided; don't do it twice
    }
};

} // namespace solero
