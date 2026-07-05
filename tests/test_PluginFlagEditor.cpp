#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QByteArray>
#include "install/PluginFlagEditor.h"

using namespace solero;

namespace {

void appendU16(QByteArray& b, quint16 v) {
    b.append(char(v & 0xFF));
    b.append(char((v >> 8) & 0xFF));
}
void appendU32(QByteArray& b, quint32 v) {
    b.append(char(v & 0xFF));
    b.append(char((v >> 8) & 0xFF));
    b.append(char((v >> 16) & 0xFF));
    b.append(char((v >> 24) & 0xFF));
}
void appendSig(QByteArray& b, const char* s) { b.append(s, 4); }

// A 6-byte subrecord header + payload.
void appendSub(QByteArray& b, const char* type, const QByteArray& data) {
    appendSig(b, type);
    appendU16(b, quint16(data.size()));
    b += data;
}

// A 24-byte record header + `dataLen` zero bytes of data.
QByteArray makeRecord(const char* sig, quint32 formId, quint32 flags = 0, int dataLen = 0) {
    QByteArray data(dataLen, '\0');
    QByteArray r;
    appendSig(r, sig);
    appendU32(r, quint32(data.size())); // dataSize
    appendU32(r, flags);                // flags
    appendU32(r, formId);               // formID
    appendU32(r, 0);                    // version control
    appendU32(r, 0);                    // form version + unknown
    r += data;
    return r;
}

// A 24-byte GRUP header wrapping `contents` (records / sub-groups).
QByteArray makeGroup(const QByteArray& contents) {
    QByteArray g;
    appendSig(g, "GRUP");
    appendU32(g, quint32(24 + contents.size())); // groupSize includes header
    appendU32(g, 0);   // label
    appendU32(g, 0);   // groupType
    appendU32(g, 0);   // stamp + unknown
    appendU32(g, 0);   // version + unknown
    g += contents;
    return g;
}

// A TES4 header record declaring `masterNames` masters (MAST subrecords).
QByteArray makeTes4(const QStringList& masterNames, quint32 recFlags = 0) {
    QByteArray data;
    appendSub(data, "HEDR", QByteArray(12, '\0'));
    for (const QString& m : masterNames) {
        QByteArray name = m.toLatin1();
        name.append('\0');
        appendSub(data, "MAST", name);
        appendSub(data, "DATA", QByteArray(8, '\0'));
    }
    QByteArray r;
    appendSig(r, "TES4");
    appendU32(r, quint32(data.size()));
    appendU32(r, recFlags);
    appendU32(r, 0); // formID
    appendU32(r, 0);
    appendU32(r, 0);
    r += data;
    return r;
}

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(bytes), qint64(bytes.size()));
    f.close();
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

} // namespace

class TestPluginFlagEditor : public QObject {
    Q_OBJECT
private slots:

    // An ESL-clean plugin: one new record (masterIndex==masterCount, objIndex<=0xFFF)
    // plus an override of a master. It should be eligible, and the flag must toggle
    // and round-trip, changing only the 4-byte flags field.
    void eligible_toggle_and_roundtrip() {
        QTemporaryDir tmp;
        const QString path = tmp.path() + "/Eligible.esp";

        QByteArray recs;
        recs += makeRecord("WEAP", 0x01000800); // new: masterIndex 1, objIndex 0x800
        recs += makeRecord("ARMO", 0x00000ABC); // override of master 0 (ignored)
        const QByteArray original = makeTes4({"Skyrim.esm"}) + makeGroup(recs);
        writeFile(path, original);

        QVERIFY(PluginFlagEditor::isTes4(path));

        const EslEligibility elig = PluginFlagEditor::checkEslEligible(path);
        QVERIFY2(elig.eligible, qPrintable(elig.reason));
        QCOMPARE(elig.newRecordCount, 1);

        bool ok = false;
        QCOMPARE(PluginFlagEditor::isLight(path, &ok), false);
        QVERIFY(ok);

        // Set the ESL flag.
        QString err;
        QVERIFY2(PluginFlagEditor::setLightFlag(path, true, &err), qPrintable(err));
        QVERIFY(PluginFlagEditor::isLight(path, &ok));

        // Backup exists and equals the original bytes (pre-edit snapshot).
        const QString backup = path + ".bak-solero";
        QVERIFY(QFile::exists(backup));
        QCOMPARE(readFile(backup), original);

        // Only the flags field (offset 8..11) changed.
        const QByteArray edited = readFile(path);
        QCOMPARE(edited.size(), original.size());
        for (int i = 0; i < edited.size(); ++i) {
            if (i >= 8 && i <= 11) continue;
            QCOMPARE(edited[i], original[i]);
        }
        // The light bit is set in the edited flags field.
        const quint32 editedFlags =
              quint8(edited[8]) | (quint32(quint8(edited[9])) << 8)
            | (quint32(quint8(edited[10])) << 16) | (quint32(quint8(edited[11])) << 24);
        QVERIFY(editedFlags & PluginFlagEditor::kLightFlag);

        // Clear it again - the file must return byte-for-byte to the original.
        QVERIFY2(PluginFlagEditor::setLightFlag(path, false, &err), qPrintable(err));
        QCOMPARE(PluginFlagEditor::isLight(path, &ok), false);
        QCOMPARE(readFile(path), original);
    }

    // A plugin with a new record whose object index exceeds 0xFFF must be refused,
    // and setLightFlag(set=true) must not modify the file or create a backup.
    void ineligible_is_refused_and_file_untouched() {
        QTemporaryDir tmp;
        const QString path = tmp.path() + "/TooBig.esp";

        QByteArray recs;
        recs += makeRecord("WEAP", 0x01005000); // new: objIndex 0x5000 > 0xFFF
        const QByteArray original = makeTes4({"Skyrim.esm"}) + makeGroup(recs);
        writeFile(path, original);

        const EslEligibility elig = PluginFlagEditor::checkEslEligible(path);
        QVERIFY(!elig.eligible);
        QVERIFY(!elig.reason.isEmpty());

        QString err;
        QVERIFY(!PluginFlagEditor::setLightFlag(path, true, &err));
        QVERIFY(!err.isEmpty());
        // Refused before any write: file unchanged, no backup, still not light.
        QCOMPARE(readFile(path), original);
        QVERIFY(!QFile::exists(path + ".bak-solero"));
        bool ok = false;
        QCOMPARE(PluginFlagEditor::isLight(path, &ok), false);
        QVERIFY(ok);
    }

    // Too many new records -> refused even if each fits the object-index range.
    void too_many_new_records_refused() {
        QTemporaryDir tmp;
        const QString path = tmp.path() + "/Crowded.esp";

        QByteArray recs;
        for (int i = 0; i <= PluginFlagEditor::kEslMaxNewRecords; ++i) // 2049 new records
            recs += makeRecord("MISC", 0x01000000u | quint32(i & 0xFFF));
        const QByteArray original = makeTes4({"Skyrim.esm"}) + makeGroup(recs);
        writeFile(path, original);

        const EslEligibility elig = PluginFlagEditor::checkEslEligible(path);
        QVERIFY(!elig.eligible);
    }

    // A corrupt record whose dataSize runs past EOF must fail safe (not eligible).
    void truncated_record_fails_safe() {
        QTemporaryDir tmp;
        const QString path = tmp.path() + "/Truncated.esp";

        QByteArray rec = makeRecord("WEAP", 0x01000010);
        // Overwrite the record's dataSize (offset 4 within the record, which sits at
        // offset [tes4 + group header] in the file) with a huge value by hand-forging
        // a record that claims more data than present.
        QByteArray bad;
        appendSig(bad, "WEAP");
        appendU32(bad, 0xFFFFFF00u); // dataSize way past EOF
        appendU32(bad, 0);
        appendU32(bad, 0x01000010);
        appendU32(bad, 0);
        appendU32(bad, 0);
        const QByteArray original = makeTes4({"Skyrim.esm"}) + makeGroup(bad);
        writeFile(path, original);

        const EslEligibility elig = PluginFlagEditor::checkEslEligible(path);
        QVERIFY(!elig.eligible);
        QVERIFY(!elig.reason.isEmpty());
    }

    // A non-TES4 file must be rejected outright.
    void non_tes4_rejected() {
        QTemporaryDir tmp;
        const QString path = tmp.path() + "/notaplugin.txt";
        writeFile(path, QByteArray("hello, not a plugin"));

        QVERIFY(!PluginFlagEditor::isTes4(path));
        QVERIFY(!PluginFlagEditor::checkEslEligible(path).eligible);
        QString err;
        QVERIFY(!PluginFlagEditor::setLightFlag(path, true, &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestPluginFlagEditor)
#include "test_PluginFlagEditor.moc"
