#pragma once
#include <QAbstractItemView>
#include <QHeaderView>
#include <QList>

// Shared column-sizing helpers for the MO2-style tables (Mods, Downloads, Saves).
//
// Layout: every column is content-fit (the width you'd get by double-clicking its
// separator), except one "fill" column (Name, or Location for Saves) which is set to
// Stretch so it eats all the remaining pane width. So the data columns stay tight to
// their content and the fill column absorbs the slack (and pane-resize slack too).
// User-set widths are persisted separately and restored, overriding these defaults.
//
// Note: because the fill column is Stretch, columns to its right are less freely
// resizable (Qt gives the space to the stretch column) - the accepted trade-off for
// "the fill column always eats the space".

namespace solero {

// Put every visible non-fill column at its content width (clamped to mins[col] and the
// header minimum), and make `fillCol` the Stretch column so it eats the rest.
inline void applyFitFillDefaults(QAbstractItemView* view, QHeaderView* hh,
                                 int fillCol, const QList<int>& mins) {
    if (!view || !hh) return;
    hh->setStretchLastSection(false); // the fill column absorbs slack, not the last one
    const int n = hh->count();
    for (int c = 0; c < n; ++c) {
        if (c == fillCol) { hh->setSectionResizeMode(c, QHeaderView::Stretch); continue; }
        hh->setSectionResizeMode(c, QHeaderView::Interactive);
        if (hh->isSectionHidden(c)) continue;
        int w = view->sizeHintForColumn(c);
        if (c < mins.size()) w = qMax(w, mins.at(c));
        w = qMax(w, hh->minimumSectionSize());
        hh->resizeSection(c, w);
    }
}

// Re-assert the fill/interactive resize modes after a restoreState() (which also
// restores modes, potentially the wrong ones). Widths from the restore are kept.
inline void assertFillMode(QHeaderView* hh, int fillCol) {
    if (!hh) return;
    hh->setStretchLastSection(false);
    const int n = hh->count();
    for (int c = 0; c < n; ++c)
        hh->setSectionResizeMode(c, c == fillCol ? QHeaderView::Stretch
                                                 : QHeaderView::Interactive);
}

} // namespace solero
