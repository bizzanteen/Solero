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

    void readMasters_parsesMastSubrecords() {
        QTemporaryDir tmp; QString s = tmp.path();
        // Build a TES4 record: 24-byte header + two MAST subrecords (each
        // followed by a DATA subrecord that must be ignored).
        auto mast = [](const QByteArray& name) {
            QByteArray nul = name; nul.append('\0');
            QByteArray sub("MAST", 4);
            quint16 sz = quint16(nul.size());
            sub.append(char(sz & 0xFF)); sub.append(char((sz >> 8) & 0xFF));
            sub.append(nul);
            return sub;
        };
        auto data8 = []() {
            QByteArray sub("DATA", 4);
            quint16 sz = 8;
            sub.append(char(sz & 0xFF)); sub.append(char((sz >> 8) & 0xFF));
            sub.append(QByteArray(8, '\0'));
            return sub;
        };
        QByteArray body;
        body += mast("Skyrim.esm"); body += data8();
        body += mast("Update.esm"); body += data8();

        QByteArray rec(24, '\0');
        rec[0]='T'; rec[1]='E'; rec[2]='S'; rec[3]='4';
        quint32 ds = quint32(body.size());
        rec[4]=char(ds & 0xFF); rec[5]=char((ds>>8)&0xFF);
        rec[6]=char((ds>>16)&0xFF); rec[7]=char((ds>>24)&0xFF);
        rec += body;

        QString path = s + "/dep.esp";
        { QFile f(path); f.open(QIODevice::WriteOnly); f.write(rec); }
        auto m = PluginScanner::readMasters(path);
        QCOMPARE(m.size(), 2);
        QCOMPARE(m[0], QString("Skyrim.esm"));
        QCOMPARE(m[1], QString("Update.esm"));

        // No-master plugin yields empty.
        writeTes4(s + "/plain.esp", 0x00000000); // dataSize 0 in 12-byte header... use 24
        QByteArray plain(24, '\0');
        plain[0]='T';plain[1]='E';plain[2]='S';plain[3]='4';
        { QFile f(s + "/plain24.esp"); f.open(QIODevice::WriteOnly); f.write(plain); }
        QVERIFY(PluginScanner::readMasters(s + "/plain24.esp").isEmpty());
        // Non-TES4 file -> empty.
        touch(s + "/junk.esp");
        QVERIFY(PluginScanner::readMasters(s + "/junk.esp").isEmpty());
    }

    void officialPlugins_baseAndCcc() {
        QTemporaryDir tmp; QString g = tmp.path();
        // No .ccc -> just the 5 base masters in order.
        auto base = PluginScanner::officialPlugins(g);
        QStringList expectedBase = {
            "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"
        };
        QCOMPARE(base, expectedBase);

        // With Skyrim.ccc, base masters first then ccc lines (blanks skipped).
        {
            QFile f(g + "/Skyrim.ccc"); f.open(QIODevice::WriteOnly);
            f.write("ccBGSSSE001-Fish.esm\n\n  ccQDRSSE001-SurvivalMode.esl  \n");
        }
        auto full = PluginScanner::officialPlugins(g);
        QStringList expectedFull = expectedBase;
        expectedFull << "ccBGSSSE001-Fish.esm" << "ccQDRSSE001-SurvivalMode.esl";
        QCOMPARE(full, expectedFull);
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
