#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "install/PluginScanner.h"
#include "core/ModList.h"
using namespace solero;
static void touch(const QString& p){ QDir().mkpath(QFileInfo(p).path()); QFile f(p); f.open(QIODevice::WriteOnly); f.write("x"); }
// Write a 12-byte TES4 header with the given little-endian record flags.
static void writeTes4(const QString& p, quint32 flags){
    QDir().mkpath(QFileInfo(p).path());
    QFile f(p); f.open(QIODevice::WriteOnly);
    QByteArray b(12, '\0');
    b[0]='T'; b[1]='E'; b[2]='S'; b[3]='4';
    b[8]  = char(flags & 0xFF);
    b[9]  = char((flags >> 8) & 0xFF);
    b[10] = char((flags >> 16) & 0xFF);
    b[11] = char((flags >> 24) & 0xFF);
    f.write(b);
}
class TestPluginScanner : public QObject { Q_OBJECT
private slots:
    void scansEnabledOnlyInOrder() {
        QTemporaryDir tmp; QString s = tmp.path();
        touch(s + "/modA/Data/Aaa.esp");
        touch(s + "/modB/Data/Bbb.esm");
        touch(s + "/modC/Data/Ccc.esp");
        ModList list;
        ModEntry a; a.type=EntryType::Mod; a.id="modA"; a.name="A"; a.enabled=true;  list.append(a);
        ModEntry b; b.type=EntryType::Mod; b.id="modB"; b.name="B"; b.enabled=true;  list.append(b);
        ModEntry c; c.type=EntryType::Mod; c.id="modC"; c.name="C"; c.enabled=false; list.append(c); // disabled
        auto plugins = PluginScanner::scan(list, s);
        QCOMPARE(plugins.size(), 2);
        QCOMPARE(plugins[0], QString("Aaa.esp"));
        QCOMPARE(plugins[1], QString("Bbb.esm"));
        QVERIFY(!plugins.contains("Ccc.esp"));
    }

    void readFlags_parsesMasterAndLight() {
        QTemporaryDir tmp; QString s = tmp.path();
        writeTes4(s + "/plain.esp",  0x00000000);
        writeTes4(s + "/master.esp", 0x00000001); // master flag on a .esp
        writeTes4(s + "/light.esp",  0x00000200); // ESL flag
        writeTes4(s + "/both.esp",   0x00000201); // master + ESL

        auto plain = PluginScanner::readFlags(s + "/plain.esp");
        QVERIFY(plain.ok); QVERIFY(!plain.isMaster); QVERIFY(!plain.isLight);
        auto master = PluginScanner::readFlags(s + "/master.esp");
        QVERIFY(master.ok); QVERIFY(master.isMaster); QVERIFY(!master.isLight);
        auto light = PluginScanner::readFlags(s + "/light.esp");
        QVERIFY(light.ok); QVERIFY(!light.isMaster); QVERIFY(light.isLight);
        auto both = PluginScanner::readFlags(s + "/both.esp");
        QVERIFY(both.ok); QVERIFY(both.isMaster); QVERIFY(both.isLight);
    }

    void readFlags_badSignatureNotOk() {
        QTemporaryDir tmp; QString s = tmp.path();
        touch(s + "/junk.esp"); // 1 byte, no TES4 sig
        auto pf = PluginScanner::readFlags(s + "/junk.esp");
        QVERIFY(!pf.ok);
        auto missing = PluginScanner::readFlags(s + "/does-not-exist.esp");
        QVERIFY(!missing.ok);
    }

    void scanGameData_baseMastersOrdered() {
        QTemporaryDir tmp; QString g = tmp.path();
        QString data = g + "/Data";
        // Base masters present out of order, plus extras to exercise bands.
        writeTes4(data + "/Dragonborn.esm", 0x00000001);
        writeTes4(data + "/Skyrim.esm",     0x00000001);
        writeTes4(data + "/Update.esm",     0x00000001);
        writeTes4(data + "/Dawnguard.esm",  0x00000001);
        writeTes4(data + "/HearthFires.esm",0x00000001);
        writeTes4(data + "/AAA.esm",        0x00000001); // other master
        writeTes4(data + "/ZZZ.esm",        0x00000001); // other master
        writeTes4(data + "/Zlight.esp",     0x00000200); // ESL-flagged
        writeTes4(data + "/Alight.esl",     0x00000200);
        writeTes4(data + "/Zmod.esp",       0x00000000);
        writeTes4(data + "/Amod.esp",       0x00000000);

        auto order = PluginScanner::scanGameData(g);
        QStringList expected = {
            "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm",
            "AAA.esm", "ZZZ.esm",       // other masters, alphabetical
            "Alight.esl", "Zlight.esp", // lights, alphabetical
            "Amod.esp", "Zmod.esp"      // esps, alphabetical
        };
        QCOMPARE(order, expected);
    }

    void scanGameData_classifiesByHeaderNotExtension() {
        QTemporaryDir tmp; QString g = tmp.path();
        QString data = g + "/Data";
        // A .esp file carrying the master header flag must sort as a master.
        writeTes4(data + "/Skyrim.esm",   0x00000001);
        writeTes4(data + "/HeaderMaster.esp", 0x00000001);
        writeTes4(data + "/Plain.esp",    0x00000000);

        auto order = PluginScanner::scanGameData(g);
        // Skyrim.esm first (base), then HeaderMaster.esp (master band), then Plain.esp.
        QStringList expected = { "Skyrim.esm", "HeaderMaster.esp", "Plain.esp" };
        QCOMPARE(order, expected);
    }
};
QTEST_MAIN(TestPluginScanner)
#include "test_PluginScanner.moc"
