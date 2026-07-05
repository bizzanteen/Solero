// Regression test for the table column-resize model used by ModListView,
// PluginListView and DownloadsTab.
//
// The proven rule (see project memory / the header comments in those views): the
// column that absorbs slack must be the last column (setStretchLastSection(true)),
// never a middle "flex" column. This test codifies WHY, by exercising the exact
// geometry with a plain QTreeView:
//
//   * Strategy F (stretch the last column): dragging ANY divider - including the
//     Name column's - follows the cursor, and total width always == viewport.
//   * A middle flex column (Stretch on a non-last section, which is what the old
//     manual fillNameColumn emulated) INVERTS every divider to its right and makes
//     the flex column itself unresizable.
//
// If someone reintroduces a middle-flex layout in a shared helper, the assertions
// here document the failure they'll reproduce.
#include <QApplication>
#include <QTreeView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
    printf("FAIL: %s (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)

enum { Enabled, Priority, Name, Version, Flags, NCols };

static QStandardItemModel* makeModel() {
    auto* m = new QStandardItemModel(6, NCols);
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < NCols; ++c)
            m->setItem(r, c, new QStandardItem(QStringLiteral("x")));
    return m;
}

// A user divider-drag of the right edge of `col` is internally resizeSection(col,
// size+dx); the header then re-solves any stretch section to keep total==viewport.
static void drag(QTreeView& v, int col, int dx) {
    auto* h = v.header();
    h->resizeSection(col, h->sectionSize(col) + dx);
}
static int boundaryRightOf(QTreeView& v, int col) {
    auto* h = v.header();
    return h->sectionViewportPosition(col) + h->sectionSize(col);
}
static int total(QTreeView& v) {
    int t = 0; for (int c = 0; c < NCols; ++c) t += v.header()->sectionSize(c); return t;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const int VW = 520;

    // ---- Strategy F: all Interactive + stretchLastSection(true) (Flags is tail) ----
    {
        QTreeView v; v.setModel(makeModel());
        auto* h = v.header();
        h->setSectionResizeMode(QHeaderView::Interactive);
        h->setStretchLastSection(true);
        v.resize(VW, 300); v.show();
        h->resizeSection(Enabled, 28); h->resizeSection(Priority, 40);
        h->resizeSection(Name, 260); h->resizeSection(Version, 80);
        h->resizeSection(Flags, 60); // explicit tail floor - mirrors the real views
        app.processEvents();

        CHECK(total(v) == v.viewport()->width(),
              "F: columns fill the pane initially (no dead space)");

        // Resize Name DIRECTLY: the grabbed boundary must follow the cursor.
        int before = boundaryRightOf(v, Name);
        drag(v, Name, +40);
        CHECK(std::abs(boundaryRightOf(v, Name) - (before + 40)) <= 3,
              "F: dragging Name's divider follows the cursor");
        CHECK(total(v) == v.viewport()->width(), "F: still fills the pane after Name drag");

        // Resize Version (a column to the LEFT of the stretch tail): follows cursor.
        h->resizeSection(Name, 260); h->resizeSection(Version, 80); app.processEvents();
        before = boundaryRightOf(v, Version);
        drag(v, Version, -30);
        CHECK(std::abs(boundaryRightOf(v, Version) - (before - 30)) <= 3,
              "F: dragging Version's divider follows the cursor");

        // Shrinking Name must not leave dead space - the tail absorbs it.
        h->resizeSection(Name, 260); app.processEvents();
        drag(v, Name, -120);
        CHECK(total(v) == v.viewport()->width(), "F: shrinking Name keeps the pane filled");
    }

    // ---- The anti-pattern: a MIDDLE stretch column inverts right-side drags ----
    {
        QTreeView v; v.setModel(makeModel());
        auto* h = v.header();
        h->setSectionResizeMode(QHeaderView::Interactive);
        h->setSectionResizeMode(Name, QHeaderView::Stretch); // Name is not last
        h->setStretchLastSection(false);
        v.resize(VW, 300); v.show();
        h->resizeSection(Enabled, 28); h->resizeSection(Priority, 40);
        h->resizeSection(Version, 80); h->resizeSection(Flags, 60);
        app.processEvents();

        // Dragging a column to the RIGHT of the middle stretch column does not
        // follow the cursor - this is the "inverted" bug we must never ship.
        int before = boundaryRightOf(v, Version);
        drag(v, Version, +30);
        CHECK(std::abs(boundaryRightOf(v, Version) - (before + 30)) > 3,
              "anti-pattern: middle-stretch inverts a right-of-flex drag (documents the bug)");
    }

    if (g_fail == 0) printf("ALL PASS\n");
    return g_fail ? 1 : 0;
}
