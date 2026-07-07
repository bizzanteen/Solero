#pragma once
#include <QAbstractItemView>
#include <QHeaderView>
#include <QList>

// Shared column-sizing helpers for the MO2-style tables (Mods, Downloads, Saves).
//
// Default layout: every column is content-fit (the same width you'd get by
// double-clicking its separator), except one "fill" column (Name) that takes the
// remaining viewport width so it's the big default. The header stays Interactive with
// stretchLastSection(true), so drags behave correctly and the rightmost column absorbs
// later pane-resize slack. User-set widths are persisted separately and restored, which
// takes precedence over these defaults.

namespace solero {

// Fit each visible non-fill column to its contents (clamped to mins[col] and the
// header's minimum), then give the fill column whatever viewport width is left.
// Call this once the view has a real width (e.g. first showEvent), only when there's
// no persisted width state to restore.
inline void applyFitFillDefaults(QAbstractItemView* view, QHeaderView* hh,
                                 int fillCol, const QList<int>& mins) {
    if (!view || !hh) return;
    const int n = hh->count();
    int used = 0;
    for (int c = 0; c < n; ++c) {
        if (c == fillCol || hh->isSectionHidden(c)) continue;
        int w = view->sizeHintForColumn(c);
        if (c < mins.size()) w = qMax(w, mins.at(c));
        w = qMax(w, hh->minimumSectionSize());
        hh->resizeSection(c, w);
        used += w;
    }
    const int avail = view->viewport() ? view->viewport()->width() : 0;
    const int floor = (fillCol < mins.size()) ? mins.at(fillCol) : 120;
    hh->resizeSection(fillCol, qMax(floor, avail - used));
}

} // namespace solero
