#include <QtTest>
#include <QTemporaryDir>
#include <QMimeData>
#include <QByteArray>
#include "ui/ModListModel.h"
#include "core/Profile.h"
using namespace solero;

// Helper: build a QMimeData carrying a source VISIBLE row in the mod-row format.
static QMimeData* modMime(int srcVisible) {
    auto* m = new QMimeData;
    m->setData("application/x-solero-mod-row", QByteArray::number(srcVisible));
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
