#pragma once
#include <QAbstractItemView>
#include <QHeaderView>
#include <QList>

// Shared column-sizing for the MO2-style tables (Mods, Downloads, Saves).
//
// The columns must always span the pane exactly: no empty gap on the right, no
// horizontal scrollbar from under-filling. Qt only guarantees that with
// stretchLastSection(true) (the rightmost column absorbs the leftover) or a Stretch
// section (which is not user-resizable). We want the fill column (Name, or Location
// for Saves) to be the big, still-resizable default, so:
//
//   - every column is Interactive (draggable),
//   - stretchLastSection(true), so the rightmost column absorbs slack -> never a gap,
//   - on layout the fill column is sized to eat the viewport, which squeezes the last
//     column down to its floor (so it never looks "massive" by default),
//   - on a pane resize the fill column is re-maximized (refillFillColumn) so widen-slack
//     goes to it, not the last column.
//
// The unavoidable Qt trade-off: the sanctioned auto-absorber is the *last* column, so
// if the user drags the fill column narrower, the last column widens to keep the pane
// full instead of leaving a gap. Persisted user widths are restored and take precedence.

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

} // namespace solero
