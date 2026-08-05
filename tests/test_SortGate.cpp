#include <QtTest>
#include "ui/SortGate.h"

using namespace solero;

// "Sort Now" is always offered. It used to be greyed out unless the user had
// manually reordered something, which meant that after editing LOOT rules -
// the most common reason to want a re-sort - there was no way to ask for one.
// A disabled menu item also can't explain itself; these reasons are resolved
// when the sort is actually requested, where they can be said out loud.
class TestSortGate : public QObject {
    Q_OBJECT
private slots:
    void sortsWhenThereIsNothingInTheWay() {
        QCOMPARE(sortGate(/*deployed=*/true, /*locked=*/false), SortGate::Go);
    }

    // Whether the user has reordered anything is deliberately not an input:
    // changing a LOOT rule changes the sort result without touching the order.
    void doesNotDependOnHavingReorderedAnythingFirst() {
        QCOMPARE(sortGate(true, false), SortGate::Go);
    }

    // libloot reads plugin headers out of the game's Data folder, so an
    // undeployed profile genuinely cannot be sorted - this one is physics, not
    // policy, and no confirmation can talk its way past it.
    void asksForADeployWhenThePluginsArentInTheGameFolder() {
        QCOMPARE(sortGate(/*deployed=*/false, /*locked=*/false), SortGate::NeedsDeploy);
    }

    // A lock is a preference, so an explicit request may override it - but only
    // after asking, since the whole point of the lock is to keep a hand-tuned
    // order.
    void asksBeforeOverridingALockedOrder() {
        QCOMPARE(sortGate(/*deployed=*/true, /*locked=*/true), SortGate::ConfirmLocked);
    }

    // Prompting "sort anyway?" and then refusing because nothing is deployed
    // would be a confirmation the user cannot act on.
    void reportsTheUnfixableReasonFirst() {
        QCOMPARE(sortGate(/*deployed=*/false, /*locked=*/true), SortGate::NeedsDeploy);
    }
};

QTEST_MAIN(TestSortGate)
#include "test_SortGate.moc"
