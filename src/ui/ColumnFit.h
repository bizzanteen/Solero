#pragma once
#include <QAbstractItemView>
#include <QHeaderView>
#include <QList>

// Shared column sizing for the MO2-style tables (Mods, Downloads, Saves).
//
// The columns have to span the pane exactly, and Qt only guarantees that via
// stretchLastSection or a Stretch section, which isn't resizable. We want the fill
// column resizable, so:
//
//   - every column Interactive,
//   - stretchLastSection(true) so the rightmost absorbs slack,
//   - the fill column sized to eat the viewport, squeezing the last to its floor,
//   - refillFillColumn on a pane resize so new slack goes to the fill column.
//
// The trade-off: drag the fill column narrower and the last one widens to keep the
// pane full. Persisted widths win over all of this.

namespace solero {

// Size the fill column to whatever viewport width is left after the other visible
// columns. Interactive, so the user can still drag it afterwards.
inline void fillColumnToViewport(QAbstractItemView* view, QHeaderView* hh,
                                 int fillCol, int fillMin) {
    if (!view || !hh || hh->isSectionHidden(fillCol)) return;
    int used = 0;
    for (int c = 0; c < hh->count(); ++c)
        if (c != fillCol && !hh->isSectionHidden(c)) used += hh->sectionSize(c);
    const int avail = view->viewport()->width() - used;
    hh->resizeSection(fillCol, qMax(avail, fillMin));
}

// Content-fit every non-fill column (clamped to mins[col] and the header minimum), keep
// all columns Interactive, give the fill column the rest of the viewport, then turn on
// stretchLastSection so the rightmost column absorbs any residual (no gap, no scrollbar).
inline void applyFitFillDefaults(QAbstractItemView* view, QHeaderView* hh,
                                 int fillCol, const QList<int>& mins) {
    if (!view || !hh) return;
    hh->setStretchLastSection(false);   // lay out by hand first; no auto-inflation
    const int n = hh->count();
    for (int c = 0; c < n; ++c) {
        hh->setSectionResizeMode(c, QHeaderView::Interactive);
        if (c == fillCol || hh->isSectionHidden(c)) continue;
        int w = view->sizeHintForColumn(c);
        if (c < mins.size()) w = qMax(w, mins.at(c));
        w = qMax(w, hh->minimumSectionSize());
        hh->resizeSection(c, w);
    }
    fillColumnToViewport(view, hh, fillCol, fillCol < mins.size() ? mins.at(fillCol) : 120);
    hh->setStretchLastSection(true);    // rightmost column absorbs the leftover from here on
}

// Cheap re-maximize of the fill column after a pane resize (no content re-fit). Collapse
// the rightmost column to its floor so the fill column - not the last one - eats the new
// slack, then let stretchLastSection guarantee the pane stays full.
inline void refillFillColumn(QAbstractItemView* view, QHeaderView* hh,
                             int fillCol, int fillMin, int lastFloor) {
    if (!view || !hh || hh->isSectionHidden(fillCol)) return;
    const int last = hh->count() - 1;
    hh->setStretchLastSection(false);
    if (last != fillCol && !hh->isSectionHidden(last))
        hh->resizeSection(last, qMax(lastFloor, hh->minimumSectionSize()));
    fillColumnToViewport(view, hh, fillCol, fillMin);
    hh->setStretchLastSection(true);
}

// Re-assert modes after restoreState() (which can restore the wrong ones). Restored
// widths are kept; stretchLastSection stays on so a narrow restored fill column still
// leaves no gap (the last column absorbs).
inline void assertFillMode(QHeaderView* hh, int /*fillCol*/) {
    if (!hh) return;
    for (int c = 0; c < hh->count(); ++c)
        hh->setSectionResizeMode(c, QHeaderView::Interactive);
    hh->setStretchLastSection(true);
}

// restoreState also restores sectionsClickable / sortIndicatorShown, so a stored
// state with clicks off makes the table permanently unsortable. Re-assert both.
inline void assertSortable(QHeaderView* hh) {
    if (!hh) return;
    hh->setSectionsClickable(true);
    hh->setSortIndicatorShown(true);
}

// Scale the non-fill columns by `ratio`, then re-fill from the viewport. Header
// widths are absolute pixels, so they have to follow the zoom-scaled font or the
// columns end up clipped or elided. Proportional, so a custom layout keeps shape.
inline void scaleColumnWidths(QAbstractItemView* view, QHeaderView* hh, double ratio,
                              int fillCol, int fillMin) {
    if (!view || !hh || ratio <= 0.0 || qFuzzyCompare(ratio, 1.0)) return;
    hh->setStretchLastSection(false);
    // Scale EVERY column (fill column included) so the layout keeps its shape even
    // when the viewport isn't laid out yet (construction-time restore, width 0).
    for (int c = 0; c < hh->count(); ++c) {
        if (hh->isSectionHidden(c)) continue;
        const int w = qRound(hh->sectionSize(c) * ratio);
        hh->resizeSection(c, qMax(hh->minimumSectionSize(), w));
    }
    // With a real viewport, true the fill column up to eat the remaining width so
    // rounding drift doesn't leave a gap/scrollbar. Skipped pre-layout (width 0),
    // where it would wrongly collapse the fill column to its floor.
    if (view->viewport()->width() > 50)
        fillColumnToViewport(view, hh, fillCol, fillMin);
    hh->setStretchLastSection(true);
}

} // namespace solero
