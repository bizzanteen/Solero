#include <QtTest>
#include <QTemporaryDir>
#include <QMimeData>
#include <QByteArray>
#include "ui/ModListModel.h"
#include "core/Profile.h"
#include "deploy/ConflictIndex.h"
using namespace solero;

// Helper: build a QMimeData carrying a source VISIBLE row in the mod-row format.
static QMimeData* modMime(int srcVisible) {
    auto* m = new QMimeData;
    m->setData("application/x-solero-mod-row", QByteArray::number(srcVisible));
    return m;
}

// Helper: build a QMimeData carrying multiple source VISIBLE rows (ascending,
// comma-separated) in the mod-row format.
static QMimeData* modMimeMulti(const QList<int>& rows) {
    QStringList parts;
    for (int r : rows) parts << QString::number(r);
    auto* m = new QMimeData;
    m->setData("application/x-solero-mod-row", parts.join(',').toLatin1());
    return m;
}

class TestModListModel : public QObject {
    Q_OBJECT
private:
    // Append n simple mods m0..m{n-1} to a profile's mod list.
    static void addMods(Profile& p, int n) {
        for (int i = 0; i < n; ++i) {
            ModEntry e; e.type = EntryType::Mod;
            e.id = QString("m%1").arg(i);
            e.name = QString("Mod%1").arg(i);
            e.enabled = true;
            p.modList().append(e);
        }
    }
    // Current raw mod-id order as a comma-joined string for easy asserts.
    static QString order(const Profile& p) {
        QStringList ids;
        for (int i = 0; i < p.modList().count(); ++i)
            ids << p.modList().at(i).id;
        return ids.join(",");
    }

private slots:
    void dropMimeData_moveDown() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addMods(prof, 4); // m0,m1,m2,m3  (+ Overwrite at visible row 4)
        ModListModel model;
        model.setProfile(&prof);
        // Visible rows: 0..3 mods, 4 = Overwrite. Drag m0 (row 0) down onto row 2.
        // Standard list move(0,2): m0 is removed then re-inserted at index 2, so it
        // lands after m2 (the dropped-on item), giving m1,m2,m0,m3.
        QScopedPointer<QMimeData> mime(modMime(0));
        QVERIFY(!model.dropMimeData(mime.data(), Qt::MoveAction, 2, 0, {}));
        QCOMPARE(order(prof), QString("m1,m2,m0,m3"));
    }

    void dropMimeData_moveUp() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addMods(prof, 4);
        ModListModel model;
        model.setProfile(&prof);
        // Move m3 (row 3) up to the top (drop at visible row 0).
        QScopedPointer<QMimeData> mime(modMime(3));
        QVERIFY(!model.dropMimeData(mime.data(), Qt::MoveAction, 0, 0, {}));
        QCOMPARE(order(prof), QString("m3,m0,m1,m2"));
    }

    void dropMimeData_dropAtBottom_landsAboveOverwrite() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addMods(prof, 4); // visible: 0..3 mods, 4 = Overwrite
        ModListModel model;
        model.setProfile(&prof);
        // Drop m0 at the very end (visible row 4 == Overwrite position). It must
        // land at the end of the real mod list, ABOVE Overwrite.
        QScopedPointer<QMimeData> mime(modMime(0));
        QVERIFY(!model.dropMimeData(mime.data(), Qt::MoveAction, 4, 0, {}));
        QCOMPARE(order(prof), QString("m1,m2,m3,m0"));
    }

    void dropMimeData_overwriteRowCannotMove() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addMods(prof, 3); // visible: 0,1,2 mods, 3 = Overwrite
        ModListModel model;
        model.setProfile(&prof);
        const QString before = order(prof);
        // Source = the Overwrite row (visible row 3). Must be a no-op.
        QScopedPointer<QMimeData> mime(modMime(3));
        QVERIFY(!model.dropMimeData(mime.data(), Qt::MoveAction, 0, 0, {}));
        QCOMPARE(order(prof), before); // unchanged
    }

    void dropMimeData_separatorSectionMoves() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        // [sepA, m0, m1, sepB, m2]  -> drag sepA below sepB's section.
        ModEntry sepA; sepA.type = EntryType::Separator; sepA.id = "sepA"; sepA.name = "A";
        ModEntry m0; m0.type = EntryType::Mod; m0.id = "m0"; m0.name = "Mod0";
        ModEntry m1; m1.type = EntryType::Mod; m1.id = "m1"; m1.name = "Mod1";
        ModEntry sepB; sepB.type = EntryType::Separator; sepB.id = "sepB"; sepB.name = "B";
        ModEntry m2; m2.type = EntryType::Mod; m2.id = "m2"; m2.name = "Mod2";
        prof.modList().append(sepA);
        prof.modList().append(m0);
        prof.modList().append(m1);
        prof.modList().append(sepB);
        prof.modList().append(m2);
        ModListModel model;
        model.setProfile(&prof);
        // Visible rows: 0=sepA,1=m0,2=m1,3=sepB,4=m2,5=Overwrite.
        // Drag sepA (row 0) to the bottom (drop at Overwrite, row 5). Whole
        // sepA section (sepA,m0,m1) must move to the end, above Overwrite.
        QScopedPointer<QMimeData> mime(modMime(0));
        QVERIFY(!model.dropMimeData(mime.data(), Qt::MoveAction, 5, 0, {}));
        QStringList ids;
        for (int i = 0; i < prof.modList().count(); ++i) ids << prof.modList().at(i).id;
        QCOMPARE(ids.join(","), QString("sepB,m2,sepA,m0,m1"));
    }

    void group_collapseHidesChildren_and_helpers() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        // [parent, c0, c1, other] (+ Overwrite). Children's parentId == parent.id.
        ModEntry parent; parent.type = EntryType::Mod; parent.id = "P0"; parent.name = "Parent";
        ModEntry c0; c0.type = EntryType::Mod; c0.id = "c0"; c0.name = "Child0"; c0.parentId = "P0";
        ModEntry c1; c1.type = EntryType::Mod; c1.id = "c1"; c1.name = "Child1"; c1.parentId = "P0";
        ModEntry other; other.type = EntryType::Mod; other.id = "u0"; other.name = "Other";
        prof.modList().append(parent);
        prof.modList().append(c0);
        prof.modList().append(c1);
        prof.modList().append(other);
        ModListModel model;
        model.setProfile(&prof);

        // Helpers.
        QVERIFY(model.isGroupParent(0));
        QVERIFY(!model.isGroupParent(3));   // "other" has no children
        QVERIFY(model.isGroupChild(1));
        QVERIFY(model.isGroupChild(2));
        QVERIFY(!model.isGroupChild(0));
        QVERIFY(!model.isGroupChild(3));
        QCOMPARE(model.groupChildCount(0), 2);

        // Expanded: parent, c0, c1, other, Overwrite = 5 visible rows.
        QCOMPARE(model.rowCount(), 5);
        QCOMPARE(model.entryAt(1)->id, QString("c0"));

        // A child row's flags must lack ItemIsDragEnabled.
        Qt::ItemFlags childFlags = model.flags(model.index(1, ModListModel::ColName));
        QVERIFY(!(childFlags & Qt::ItemIsDragEnabled));
        // Parent row stays draggable.
        QVERIFY(model.flags(model.index(0, ModListModel::ColName)) & Qt::ItemIsDragEnabled);

        // Collapse the parent -> children hidden. parent, other, Overwrite = 3.
        model.toggleModCollapse(0);
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.entryAt(0)->id, QString("P0"));
        QCOMPARE(model.entryAt(1)->id, QString("u0"));

        // Expand again -> back to 5.
        model.toggleModCollapse(0);
        QCOMPARE(model.rowCount(), 5);
    }

    void group_dragParentMovesWholeBlock() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        // [parent, c0, c1, other] (+ Overwrite). Drag parent to the bottom.
        ModEntry parent; parent.type = EntryType::Mod; parent.id = "P0"; parent.name = "Parent";
        ModEntry c0; c0.type = EntryType::Mod; c0.id = "c0"; c0.name = "Child0"; c0.parentId = "P0";
        ModEntry c1; c1.type = EntryType::Mod; c1.id = "c1"; c1.name = "Child1"; c1.parentId = "P0";
        ModEntry other; other.type = EntryType::Mod; other.id = "u0"; other.name = "Other";
        prof.modList().append(parent);
        prof.modList().append(c0);
        prof.modList().append(c1);
        prof.modList().append(other);
        ModListModel model;
        model.setProfile(&prof);
        // Visible: 0=parent,1=c0,2=c1,3=other,4=Overwrite. Drop parent at Overwrite.
        QScopedPointer<QMimeData> mime(modMime(0));
        QVERIFY(!model.dropMimeData(mime.data(), Qt::MoveAction, 4, 0, {}));
        // Whole group moves below "other", children staying contiguous after parent.
        QCOMPARE(order(prof), QString("u0,P0,c0,c1"));
    }

    void group_dropSingleModBetweenChildren_landsAfterGroup() {
        // Regression: dragging a lone mod into the MIDDLE of an expanded group must
        // not split the group. It should land after the whole child run.
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        // [x, parent, c0, c1] (+ Overwrite). x is a plain mod; c0,c1 belong to parent.
        ModEntry x; x.type = EntryType::Mod; x.id = "x"; x.name = "X";
        ModEntry parent; parent.type = EntryType::Mod; parent.id = "P0"; parent.name = "Parent";
        ModEntry c0; c0.type = EntryType::Mod; c0.id = "c0"; c0.name = "Child0"; c0.parentId = "P0";
        ModEntry c1; c1.type = EntryType::Mod; c1.id = "c1"; c1.name = "Child1"; c1.parentId = "P0";
        prof.modList().append(x);
        prof.modList().append(parent);
        prof.modList().append(c0);
        prof.modList().append(c1);
        ModListModel model;
        model.setProfile(&prof);
        // Visible: 0=x,1=parent,2=c0,3=c1,4=Overwrite. Drop x between c0 and c1
        // (visible row 3). It must snap past the group, never splitting it.
        QScopedPointer<QMimeData> mime(modMime(0));
        QVERIFY(!model.dropMimeData(mime.data(), Qt::MoveAction, 3, 0, {}));
        // x lands after the group: P0,c0,c1,x - never P0,c0,x,c1.
        QCOMPARE(order(prof), QString("P0,c0,c1,x"));
    }

    void stateFilterSources_reflectConflictsUpdatesDeps() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addMods(prof, 3); // m0,m1,m2
        ModListModel model;
        model.setProfile(&prof);

        // No data yet -> all predicates false.
        QVERIFY(!model.modHasConflict("m0"));
        QVERIFY(!model.modHasUpdate("m0"));
        QVERIFY(!model.modHasMissingDep("m0"));

        // Conflict: m0 wins "a.esp" over m1 (m0 overwrites, m1 overwritten).
        ConflictIndex ci;
        ci.setWinner("a.esp", "m0");
        ci.recordConflict("a.esp", "m0", "m1");
        model.setConflictIndex(ci);
        QVERIFY(model.modHasConflict("m0"));  // winner
        QVERIFY(model.modHasConflict("m1"));  // loser
        QVERIFY(!model.modHasConflict("m2")); // uninvolved

        // Update available for m1.
        QHash<QString, QPair<QString,QString>> updates;
        updates.insert("m1", {"1.0", "1.1"});
        model.setUpdateInfo(updates);
        QVERIFY(model.modHasUpdate("m1"));
        QVERIFY(!model.modHasUpdate("m0"));

        // Missing dependency on m2.
        QHash<QString,QStringList> deps;
        deps.insert("m2", {"Requires SkyUI"});
        model.setDependencyWarnings(deps);
        QVERIFY(model.modHasMissingDep("m2"));
        QVERIFY(!model.modHasMissingDep("m0"));
    }

    // --- Nested separators (sub-categories) -----------------------------------
    // Build [sepA(L0), mA0, sepB(L1), mB0, sepC(L0), mC0] into a profile.
    static void addNested(Profile& p) {
        auto sep = [](const QString& id, const QString& name, int lvl) {
            ModEntry e; e.type = EntryType::Separator; e.id = id; e.name = name; e.separatorLevel = lvl; return e;
        };
        auto mod = [](const QString& id) {
            ModEntry e; e.type = EntryType::Mod; e.id = id; e.name = "Mod " + id; e.enabled = true; return e;
        };
        p.modList().append(sep("sepA", "A", 0));
        p.modList().append(mod("mA0"));
        p.modList().append(sep("sepB", "B", 1));
        p.modList().append(mod("mB0"));
        p.modList().append(sep("sepC", "C", 0));
        p.modList().append(mod("mC0"));
    }
    static QStringList visibleIds(const ModListModel& m) {
        QStringList ids;
        for (int r = 0; r < m.rowCount(); ++r) {
            const auto* e = m.entryAt(r);
            ids << (e ? e->id : QString("__overwrite__"));
        }
        return ids;
    }

    void nestedSeparator_collapseParentHidesSubSeparatorsAndMods() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addNested(prof);
        ModListModel model;
        model.setProfile(&prof);
        // Expanded: sepA,mA0,sepB,mB0,sepC,mC0,Overwrite = 7.
        QCOMPARE(model.rowCount(), 7);
        // Collapse sepA (level 0, visible row 0): hides mA0 and the sub-separator
        // sepB and its mB0, up to the next level<=0 separator (sepC).
        model.toggleCollapse(0);
        QCOMPARE(visibleIds(model),
                 QStringList({"sepA", "sepC", "mC0", "__overwrite__"}));
    }

    void nestedSeparator_collapseChildHidesOnlyItsMods() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addNested(prof);
        ModListModel model;
        model.setProfile(&prof);
        // Collapse the sub-category sepB (level 1, visible row 2): hides only mB0,
        // stopping at sepC (level 0 <= 1). Parent sepA stays expanded.
        model.toggleCollapse(2);
        QCOMPARE(visibleIds(model),
                 QStringList({"sepA", "mA0", "sepB", "sepC", "mC0", "__overwrite__"}));
    }

    void nestedSeparator_dragLevel0CarriesSubtree() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addNested(prof);
        ModListModel model;
        model.setProfile(&prof);
        // Drag sepA (visible row 0) to the bottom (drop at Overwrite, row 6). The
        // whole subtree (sepA,mA0,sepB,mB0) moves below sepC's section.
        QScopedPointer<QMimeData> mime(modMime(0));
        QVERIFY(!model.dropMimeData(mime.data(), Qt::MoveAction, 6, 0, {}));
        QCOMPARE(order(prof), QString("sepC,mC0,sepA,mA0,sepB,mB0"));
        // Relative levels preserved within the moved block.
        QCOMPARE(prof.modList().findById("sepA")->separatorLevel, 0);
        QCOMPARE(prof.modList().findById("sepB")->separatorLevel, 1);
    }

    void nestedSeparator_dragLevel1CarriesOnlyOwnMods() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addNested(prof);
        ModListModel model;
        model.setProfile(&prof);
        // Drag the sub-category sepB (visible row 2) to the bottom. Only sepB + its
        // own mod mB0 move (boundary = next separator with level <= 1, i.e. sepC).
        QScopedPointer<QMimeData> mime(modMime(2));
        QVERIFY(!model.dropMimeData(mime.data(), Qt::MoveAction, 6, 0, {}));
        QCOMPARE(order(prof), QString("sepA,mA0,sepC,mC0,sepB,mB0"));
    }

    void dropMimeData_multiNonContiguous_dropsAsBlock() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addMods(prof, 5); // m0,m1,m2,m3,m4 (+ Overwrite at visible row 5)
        ModListModel model;
        model.setProfile(&prof);
        // Select non-adjacent m1 (row 1) and m3 (row 3); drop at the front (row 0).
        // They lift together and land as a contiguous block in original order,
        // giving m1,m3,m0,m2,m4.
        QScopedPointer<QMimeData> mime(modMimeMulti({1, 3}));
        QVERIFY(!model.dropMimeData(mime.data(), Qt::MoveAction, 0, 0, {}));
        QCOMPARE(order(prof), QString("m1,m3,m0,m2,m4"));
    }

    void searchExpandAll_revealsCollapsedSectionMods_withoutMutatingState() {
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        // [sepA(L0, collapsed), m0, m1, sepB(L0), m2]. sepA collapsed -> m0,m1 hidden.
        ModEntry sepA; sepA.type = EntryType::Separator; sepA.id = "sepA";
        sepA.name = "A"; sepA.separatorLevel = 0; sepA.collapsed = true;
        ModEntry m0; m0.type = EntryType::Mod; m0.id = "m0"; m0.name = "Mod0"; m0.enabled = true;
        ModEntry m1; m1.type = EntryType::Mod; m1.id = "m1"; m1.name = "Mod1"; m1.enabled = true;
        ModEntry sepB; sepB.type = EntryType::Separator; sepB.id = "sepB";
        sepB.name = "B"; sepB.separatorLevel = 0;
        ModEntry m2; m2.type = EntryType::Mod; m2.id = "m2"; m2.name = "Mod2"; m2.enabled = true;
        prof.modList().append(sepA);
        prof.modList().append(m0);
        prof.modList().append(m1);
        prof.modList().append(sepB);
        prof.modList().append(m2);
        ModListModel model;
        model.setProfile(&prof);

        // Default (collapse-aware): sepA collapsed hides m0,m1.
        // Visible: sepA, sepB, m2, Overwrite.
        QCOMPARE(visibleIds(model),
                 QStringList({"sepA", "sepB", "m2", "__overwrite__"}));

        // Search expand-all: every entry becomes a visible row, collapsed mods included.
        model.setSearchExpandAll(true);
        QCOMPARE(visibleIds(model),
                 QStringList({"sepA", "m0", "m1", "sepB", "m2", "__overwrite__"}));
        // Persisted collapse state must be untouched.
        QVERIFY(prof.modList().findById("sepA")->collapsed);

        // Clearing the flag returns to the collapse-aware view.
        model.setSearchExpandAll(false);
        QCOMPARE(visibleIds(model),
                 QStringList({"sepA", "sepB", "m2", "__overwrite__"}));
        QVERIFY(prof.modList().findById("sepA")->collapsed);
    }

    void moveRows_endMappingAppends() {
        // Direct moveRows: dst at the Overwrite visible row maps to end-of-list.
        QTemporaryDir tmp;
        Profile prof("P", tmp.path());
        addMods(prof, 3); // visible 0,1,2 mods; 3 = Overwrite
        ModListModel model;
        model.setProfile(&prof);
        QVERIFY(model.moveRows({}, 0, 1, {}, 3)); // move m0 to the end slot
        QCOMPARE(order(prof), QString("m1,m2,m0"));
    }

};
QTEST_MAIN(TestModListModel)
#include "test_ModListModel.moc"
