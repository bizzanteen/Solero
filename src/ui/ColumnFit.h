#pragma once
#include <QAbstractItemView>
#include <QHeaderView>
#include <QList>

// Shared column-sizing for the MO2-style tables (Mods, Downloads, Saves).
//
// Every column is Interactive (user-resizable). Non-fill columns start content-fit
// (the width a double-click on the separator gives), clamped to a floor. The fill
// column (Name, or Location for Saves) takes the remaining viewport so it is the big
// default, and callers re-run fillColumnToViewport() on resize so it keeps absorbing
// the slack. Nothing is Stretch, so every divider drag follows the cursor and the fill
// column stays resizable. Persisted user widths are restored and take precedence.

namespace solero {

// Set the fill column to whatever viewport width is left after the other visible
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

// Content-fit every non-fill column (clamped to mins[col] and the header minimum),
// keep all columns Interactive, and let the fill column take the rest of the viewport.
inline void applyFitFillDefaults(QAbstractItemView* view, QHeaderView* hh,
                                 int fillCol, const QList<int>& mins) {
    if (!view || !hh) return;
    hh->setStretchLastSection(false);
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
}

// Re-assert Interactive modes after restoreState() (which can restore Stretch or the
// wrong modes). Restored widths are kept as-is.
inline void assertFillMode(QHeaderView* hh, int /*fillCol*/) {
    if (!hh) return;
    hh->setStretchLastSection(false);
    for (int c = 0; c < hh->count(); ++c)
        hh->setSectionResizeMode(c, QHeaderView::Interactive);
}

} // namespace solero
