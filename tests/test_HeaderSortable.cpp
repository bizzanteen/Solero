// restoreState carries sectionsClickable and sortIndicatorShown. A persisted state
// with clicks off makes the table permanently unsortable, which is what the live
// downloads header state had in it.
#include <QtTest>
#include <QTableWidget>
#include <QHeaderView>
#include "ui/ColumnFit.h"

class TestHeaderSortable : public QObject {
    Q_OBJECT
private slots:
    void restoringAStateWithClicksOffLeavesTheHeaderDead() {
        QTableWidget src(0, 4);
        src.setSortingEnabled(true);
        src.horizontalHeader()->setSectionsClickable(false);
        const QByteArray bad = src.horizontalHeader()->saveState();

        QTableWidget t(0, 4);
        t.setSortingEnabled(true);
        t.horizontalHeader()->restoreState(bad);
        QVERIFY2(!t.horizontalHeader()->sectionsClickable(),
                 "expected restoreState to have clobbered sectionsClickable");

        solero::assertSortable(t.horizontalHeader());
        QVERIFY(t.horizontalHeader()->sectionsClickable());
        QVERIFY(t.horizontalHeader()->isSortIndicatorShown());
    }

    // Widths and the user's chosen sort still have to survive the re-assert.
    void keepsRestoredWidthsAndSortIndicator() {
        QTableWidget src(0, 4);
        src.setSortingEnabled(true);
        src.horizontalHeader()->resizeSection(0, 321);
        src.sortByColumn(2, Qt::AscendingOrder);
        const QByteArray good = src.horizontalHeader()->saveState();

        QTableWidget t(0, 4);
        t.setSortingEnabled(true);
        t.horizontalHeader()->restoreState(good);
        solero::assertFillMode(t.horizontalHeader(), 0);
        solero::assertSortable(t.horizontalHeader());

        QCOMPARE(t.horizontalHeader()->sectionSize(0), 321);
        QCOMPARE(t.horizontalHeader()->sortIndicatorSection(), 2);
        QCOMPARE(t.horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);
        QVERIFY(t.horizontalHeader()->sectionsClickable());
    }

    void isSafeOnANullHeader() {
        solero::assertSortable(nullptr);
    }
};

QTEST_MAIN(TestHeaderSortable)
#include "test_HeaderSortable.moc"
