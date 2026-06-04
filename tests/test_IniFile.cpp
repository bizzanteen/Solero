#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "bethini/IniFile.h"
using namespace solero;

class TestIniFile : public QObject {
    Q_OBJECT
private slots:
    // A space-key like "iSize W" must round-trip verbatim (not percent-encoded
    // like QSettings would: "iSize%20W"), and comments/untouched lines survive.
    void spaceKey_roundTrips_preservesComments() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/Skyrim.ini";
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("; display settings\n"
                    "[Display]\n"
                    "iSize W=1280\n"
                    "iSize H=720\n"
                    "bFull Screen=0\n");
            f.close();
        }

        IniFile ini;
        QVERIFY(ini.load(path));
        QCOMPARE(ini.value("Display", "iSize W"), QString("1280"));
        QCOMPARE(ini.value("Display", "iSize H"), QString("720"));
        QVERIFY(ini.has("Display", "bFull Screen"));
        QVERIFY(!ini.dirty());

        // Change one space-key; the comment and untouched lines must survive.
        ini.setValue("Display", "iSize W", "1920");
        QVERIFY(ini.dirty());
        QVERIFY(ini.save(path));

        QFile r(path);
        QVERIFY(r.open(QIODevice::ReadOnly));
        QString out = QString::fromUtf8(r.readAll());
        r.close();

        // No percent-encoding anywhere.
        QVERIFY(!out.contains("%20"));
        QVERIFY(out.contains("iSize W=1920"));
        // Comment preserved.
        QVERIFY(out.contains("; display settings"));
        // Untouched lines byte-identical.
        QVERIFY(out.contains("\niSize H=720\n"));
        QVERIFY(out.contains("\nbFull Screen=0\n"));
    }

    // setValue with an identical value must not mark the file dirty.
    void setValue_noChange_notDirty() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/s.ini";
        { QFile f(path); f.open(QIODevice::WriteOnly); f.write("[A]\nk=5\n"); f.close(); }
        IniFile ini;
        QVERIFY(ini.load(path));
        ini.setValue("A", "k", "5");
        QVERIFY(!ini.dirty());
        ini.setValue("A", "k", "6");
        QVERIFY(ini.dirty());
    }

    // Missing file is an empty doc; creating section + key works.
    void missingFile_createsSectionAndKey() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/new.ini";
        IniFile ini;
        QVERIFY(ini.load(path)); // missing -> empty doc, true
        QVERIFY(!ini.has("General", "bFoo"));
        ini.setValue("General", "bFoo", "1");
        QVERIFY(ini.dirty());
        QCOMPARE(ini.value("General", "bFoo"), QString("1"));
        QVERIFY(ini.save(path));

        IniFile re;
        QVERIFY(re.load(path));
        QCOMPARE(re.value("General", "bFoo"), QString("1"));
    }

    // Inserting a new key into an existing section places it after the last key.
    void insertKey_intoExistingSection() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/i.ini";
        { QFile f(path); f.open(QIODevice::WriteOnly);
          f.write("[General]\na=1\nb=2\n\n[Other]\nx=9\n"); f.close(); }
        IniFile ini;
        QVERIFY(ini.load(path));
        ini.setValue("General", "c", "3");
        QCOMPARE(ini.value("General", "c"), QString("3"));
        QCOMPARE(ini.value("Other", "x"), QString("9"));
        QVERIFY(ini.save(path));
        QFile r(path); r.open(QIODevice::ReadOnly);
        QString out = QString::fromUtf8(r.readAll());
        // c must land inside [General], before [Other].
        QVERIFY(out.indexOf("c=3") < out.indexOf("[Other]"));
        QVERIFY(out.indexOf("c=3") > out.indexOf("b=2"));
    }

    // Key lookup is case-insensitive (Skyrim is lenient).
    void keyLookup_caseInsensitive() {
        IniFile ini;
        ini.setValue("Display", "iSize W", "800");
        QCOMPARE(ini.value("display", "isize w"), QString("800"));
        // Updating via different case preserves the original spelling.
        ini.setValue("DISPLAY", "ISIZE W", "640");
        QString path;
        QTemporaryDir tmp;
        path = tmp.path() + "/c.ini";
        QVERIFY(ini.save(path));
        QFile r(path); r.open(QIODevice::ReadOnly);
        QString out = QString::fromUtf8(r.readAll());
        QVERIFY(out.contains("iSize W=640")); // original spelling kept
    }
};
QTEST_MAIN(TestIniFile)
#include "test_IniFile.moc"
