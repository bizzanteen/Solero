#pragma once
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QApplication>
#include <QStyle>

namespace solero {

// Item delegate that elides overlong cell text at CHARACTER precision
// ("Some Long Mod Na…") rather than at word boundaries ("Some Long…").
//
// Qt's built-in item text rendering lays the string out through QTextLayout,
// which wraps at word boundaries before eliding the final line - so a long name
// with spaces gets cut back to the last whole word that fits, then an ellipsis.
// Setting the view's textElideMode alone does not change that. Pre-eliding the
// text here with QFontMetrics (which truncates per character) and disabling the
// style's own elision gives the expected char-level "…".
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
