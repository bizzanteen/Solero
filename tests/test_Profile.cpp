#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "core/ProfileManager.h"
using namespace solero;

class TestProfile : public QObject {
    Q_OBJECT
private slots:
    void createProfile_dirExists() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("TestProfile");
        QVERIFY(QDir(tmp.path() + "/TestProfile").exists());
    }
    void listProfiles_returnsCreated() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("Alpha");
        mgr.createProfile("Beta");
        auto names = mgr.profileNames();
        QVERIFY(names.contains("Alpha"));
        QVERIFY(names.contains("Beta"));
    }
    void switchProfile_loadsCorrectData() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("P1");
        Profile* p = mgr.loadProfile("P1");
        QVERIFY(p != nullptr);
        QCOMPARE(p->name(), QString("P1"));
    }
    void deleteProfile_removesDir() {
        QTemporaryDir tmp;
        ProfileManager mgr(tmp.path());
        mgr.createProfile("ToDelete");
        QVERIFY(mgr.deleteProfile("ToDelete"));
        QVERIFY(!QDir(tmp.path() + "/ToDelete").exists());
    }
    void fileRules_roundTrip() {
        QTemporaryDir tmp;
        {
            Profile p("Rules", tmp.path());
            p.setFileHidden("modA", "Data/x.dll", true);
            p.setFileHidden("modA", "Data/SKSE/y.esp", true);
            p.setWinnerOverride("Data/z.nif", "modB");
            QVERIFY(p.save());
            QVERIFY(QFile::exists(p.fileRulesPath()));
        }
        // Reload into a fresh Profile and confirm the rules survived.
        Profile p2("Rules", tmp.path());
        QVERIFY(p2.load());
        QVERIFY(p2.isFileHidden("modA", "Data/x.dll"));
        QVERIFY(p2.isFileHidden("modA", "Data/SKSE/y.esp"));
        QVERIFY(!p2.isFileHidden("modA", "Data/other.dll"));
        QCOMPARE(p2.winnerOverride("Data/z.nif"), QString("modB"));
        QVERIFY(p2.winnerOverride("Data/none.nif").isEmpty());

        // Unhide + clear override, save, reload - the rules are gone (sparse).
        p2.setFileHidden("modA", "Data/x.dll", false);
        p2.clearWinnerOverride("Data/z.nif");
        QVERIFY(p2.save());
        Profile p3("Rules", tmp.path());
        QVERIFY(p3.load());
        QVERIFY(!p3.isFileHidden("modA", "Data/x.dll"));
        QVERIFY(p3.isFileHidden("modA", "Data/SKSE/y.esp")); // the other one stays
        QVERIFY(p3.winnerOverride("Data/z.nif").isEmpty());
    }
    void fileRules_missingFileIsEmpty() {
        QTemporaryDir tmp;
        Profile p("NoRules", tmp.path());
        QVERIFY(p.load()); // no filerules.json -> backward compatible, no rules
        QVERIFY(!p.isFileHidden("anyMod", "Data/x.dll"));
        QVERIFY(p.winnerOverride("Data/x.dll").isEmpty());
    }
};
QTEST_MAIN(TestProfile)
#include "test_Profile.moc"
