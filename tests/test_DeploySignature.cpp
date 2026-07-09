// The deploy signature (core/DeploySignature.h) lets the UI flip back to "Deployed"
// after a net-zero structural change. It must be identical for two profiles that
// deploy the same files and differ when the deployed set would differ.
#include <QtTest>
#include <QTemporaryDir>
#include "core/DeploySignature.h"
#include "core/Profile.h"
using namespace solero;

class TestDeploySignature : public QObject {
    Q_OBJECT
    static ModEntry mod(const QString& id) {
        ModEntry m; m.type = EntryType::Mod; m.id = id; m.name = id; m.enabled = true; return m;
    }
private slots:
    // Disable then re-enable a mod: the enabled set and order are unchanged, so the
    // signature returns to its original value (the button can flip back to Deployed).
    void disableThenEnable_isNetZero() {
        QTemporaryDir tmp;
        Profile p("T", tmp.path());
        p.modList().append(mod("a"));
        p.modList().append(mod("b"));
        const QByteArray base = deploySignature(p);

        p.modList().setEnabled("b", false);
        QVERIFY(deploySignature(p) != base);   // disabled: fewer mods deploy
        p.modList().setEnabled("b", true);
        QCOMPARE(deploySignature(p), base);    // re-enabled in place: back to net-zero
    }

    // Reorder changes priority (and thus conflict winners), so the signature differs;
    // reordering back restores it.
    void reorder_changesThenRestores() {
        QTemporaryDir tmp;
        Profile p("T", tmp.path());
        p.modList().append(mod("a"));
        p.modList().append(mod("b"));
        const QByteArray base = deploySignature(p);
        p.modList().move(0, 1);
        QVERIFY(deploySignature(p) != base);
        p.modList().move(0, 1);
        QCOMPARE(deploySignature(p), base);
    }

};

QTEST_MAIN(TestDeploySignature)
#include "test_DeploySignature.moc"
