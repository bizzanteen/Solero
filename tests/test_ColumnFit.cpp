// Regression test for the shared column-fit helpers (ui/ColumnFit.h) used by the
// Mods, Downloads and Saves tables.
//
// Bug this locks in: applyFitFillDefaults() used to set the fill column (Name) to
// QHeaderView::Stretch, which Qt makes non-draggable - so the user could not resize
// Name. The fix keeps EVERY column Interactive (resizable) while the fill column still
// takes the leftover viewport width by default, and fillColumnToViewport() keeps it
// absorbing pane-resize / zoom slack.
//
// Assertions:
//   (a) after applyFitFillDefaults no section is Stretch (all Interactive),
//   (b) the fill column is itself resizable (Interactive),
//   (c) the fill column is the widest and the columns roughly span the viewport,
//   (d) shrinking the viewport + fillColumnToViewport shrinks the fill column.
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

    // (a) nothing is Stretch; every section is Interactive.
    for (int c = 0; c < hh->count(); ++c)
        CHECK(hh->sectionResizeMode(c) == QHeaderView::Interactive,
              "every column is Interactive (no Stretch section)");

    // (b) the fill column is resizable.
    CHECK(hh->sectionResizeMode(fillCol) == QHeaderView::Interactive,
          "the fill column is Interactive (user-resizable)");

    // (c) the fill column is the widest, and the columns roughly span the viewport.
    const int vw = table.viewport()->width();
    for (int c = 1; c < hh->count(); ++c)
        CHECK(hh->sectionSize(fillCol) > hh->sectionSize(c),
              "the fill column is the widest column");
    CHECK(std::abs(totalWidth(hh) - vw) <= 4,
          "the columns roughly span the viewport after applyFitFillDefaults");

    const int wideFill = hh->sectionSize(fillCol);

    // (d) shrinking the viewport + fillColumnToViewport shrinks the fill column.
    table.resize(360, 300);
    app.processEvents();
    solero::fillColumnToViewport(&table, hh, fillCol, mins.at(fillCol));
    app.processEvents();
    CHECK(hh->sectionSize(fillCol) < wideFill,
          "fillColumnToViewport shrinks the fill column when the viewport shrinks");
    CHECK(std::abs(totalWidth(hh) - table.viewport()->width()) <= 4,
          "the columns still span the (smaller) viewport after fillColumnToViewport");
    CHECK(hh->sectionSize(fillCol) >= mins.at(fillCol),
          "the fill column never shrinks below its floor");

    if (g_fail == 0) printf("ALL PASS\n");
    return g_fail ? 1 : 0;
}
