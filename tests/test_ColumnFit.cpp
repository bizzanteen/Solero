// Column-fit helpers (ui/ColumnFit.h) for the Mods, Downloads and Saves tables.
//
// Locks in the fix for: applyFitFillDefaults() set the fill column (Name) to
// QHeaderView::Stretch, which Qt makes non-draggable, so Name could not be resized.
// Every column is Interactive now, with the fill column taking the leftover width.
#include "ui/ColumnFit.h"
#include <QApplication>
#include <QTableWidget>
#include <QHeaderView>
#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    printf("FAIL: %s (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)

static int totalWidth(QHeaderView* h) {
    int t = 0;
    for (int c = 0; c < h->count(); ++c)
        if (!h->isSectionHidden(c)) t += h->sectionSize(c);
    return t;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QTableWidget table;
    table.setColumnCount(4); // Name(fill), Status, Size, Date
    table.setRowCount(3);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 4; ++c)
            table.setItem(r, c, new QTableWidgetItem(QStringLiteral("x")));
    table.verticalHeader()->hide(); // keep the viewport width predictable
    auto* hh = table.horizontalHeader();
    hh->setMinimumSectionSize(20);
    table.resize(600, 300);
    table.show();
    app.processEvents();

    const int fillCol = 0;
    const QList<int> mins = {150, 60, 60, 60};
    solero::applyFitFillDefaults(&table, hh, fillCol, mins);
    app.processEvents();

    // (a) every section is Interactive (draggable) and stretchLastSection is on so the
    // pane is always filled.
    for (int c = 0; c < hh->count(); ++c)
        CHECK(hh->sectionResizeMode(c) == QHeaderView::Interactive,
              "every column is Interactive (no Stretch mode)");
    CHECK(hh->stretchLastSection(),
          "stretchLastSection is on so the pane is never left with a gap");

    // (b) the fill column is resizable.
    CHECK(hh->sectionResizeMode(fillCol) == QHeaderView::Interactive,
          "the fill column is Interactive (user-resizable)");

    // (c) the fill column is the widest, and the columns span the viewport exactly.
    const int vw = table.viewport()->width();
    for (int c = 1; c < hh->count(); ++c)
        CHECK(hh->sectionSize(fillCol) > hh->sectionSize(c),
              "the fill column is the widest column");
    CHECK(std::abs(totalWidth(hh) - vw) <= 2,
          "the columns span the viewport after applyFitFillDefaults (no gap)");

    const int wideFill = hh->sectionSize(fillCol);

    // (d) shrinking the viewport: the real view re-fits on resize (refillFillColumn),
    // so the fill column shrinks and the columns still span the pane (no gap, no
    // overflow) down to the fill floor.
    table.resize(360, 300);
    app.processEvents();
    solero::refillFillColumn(&table, hh, fillCol, mins.at(fillCol), mins.last());
    app.processEvents();
    CHECK(hh->sectionSize(fillCol) < wideFill,
          "the fill column shrinks when the viewport shrinks");
    CHECK(std::abs(totalWidth(hh) - table.viewport()->width()) <= 2,
          "the columns still span the smaller viewport (no gap)");

    // (e) widening the viewport + refillFillColumn re-maximizes the FILL column (widen
    // slack goes to it, not the last column) while the pane stays full.
    table.resize(800, 300);
    app.processEvents();
    solero::refillFillColumn(&table, hh, fillCol, mins.at(fillCol), mins.last());
    app.processEvents();
    CHECK(hh->sectionSize(fillCol) > wideFill,
          "refillFillColumn grows the fill column when the viewport widens");
    CHECK(hh->sectionSize(hh->count() - 1) <= mins.last() + 4,
          "the last column stays at its floor (the fill column absorbed the slack)");
    CHECK(std::abs(totalWidth(hh) - table.viewport()->width()) <= 2,
          "the columns span the wider viewport exactly (no gap)");
    CHECK(hh->sectionSize(fillCol) >= mins.at(fillCol),
          "the fill column never shrinks below its floor");

    if (g_fail == 0) printf("ALL PASS\n");
    return g_fail ? 1 : 0;
}
