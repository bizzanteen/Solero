#include "saves/SaveFile.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QByteArray>
#include <QtEndian>

#ifdef SOLERO_HAVE_ZLIB
#include <zlib.h>
#endif

using namespace solero;

namespace {

// Little-endian byte-buffer builder mirroring the .ess on-disk layout.
struct Builder {
    QByteArray b;
    void u8(quint8 v)  { b.append(char(v)); }
    void u16(quint16 v){ uchar t[2]; qToLittleEndian(v, t); b.append(reinterpret_cast<char*>(t), 2); }
    void u32(quint32 v){ uchar t[4]; qToLittleEndian(v, t); b.append(reinterpret_cast<char*>(t), 4); }
    void u64(quint64 v){ uchar t[8]; qToLittleEndian(v, t); b.append(reinterpret_cast<char*>(t), 8); }
    void wstr(const QString& s) { QByteArray a = s.toLatin1(); u16(quint16(a.size())); b.append(a); }
    void raw(const QByteArray& a) { b.append(a); }
};

// The body's plugin block: formVersion u8, pluginInfoSize u32, count u8, wstrings.
QByteArray pluginBlock(const QStringList& plugins) {
    Builder inner;
    inner.u8(quint8(plugins.size()));
    for (const QString& p : plugins) inner.wstr(p);
    Builder pb;
    pb.u8(74); // formVersion
    pb.u32(quint32(inner.b.size()));
    pb.raw(inner.b);
    return pb.b;
}

// Assemble a full save. version>=12 => SSE (4bpp screenshot + compressionType
// field); otherwise LE (3bpp, no compressionType). `body` is appended after the
// screenshot verbatim (an uncompressed plugin block, or a compressed wrapper).
QByteArray buildSave(quint32 version, const QString& name, quint32 level,
                     const QString& location, const QString& date, const QString& race,
                     quint16 sex, quint32 saveNum, quint32 w, quint32 h,
                     quint16 compressionType, const QByteArray& body,
                     char shotFill = '\x5a') {
    Builder hdr;
    hdr.u32(version);
    hdr.u32(saveNum);
    hdr.wstr(name);
    hdr.u32(level);
    hdr.wstr(location);
    hdr.wstr(date);
    hdr.wstr(race);
    hdr.u16(sex);
    hdr.u32(0);                       // playerCurExp (f32 bytes, unused)
    hdr.u32(0);                       // playerLvlUpExp (f32 bytes, unused)
    hdr.u64(0x01D8000000000000ULL);   // filetime
    hdr.u32(w);
    hdr.u32(h);
    if (version >= 12) hdr.u16(compressionType);

    Builder save;
    save.raw(QByteArray("TESV_SAVEGAME", 13));
    save.u32(quint32(hdr.b.size()));  // headerSize
    save.raw(hdr.b);
    const int bpp = (version >= 12) ? 4 : 3;
    save.raw(QByteArray(int(w * h * bpp), shotFill)); // screenshot pixels
    save.raw(body);
    return save.b;
}

QString writeTemp(QTemporaryDir& dir, const QString& fn, const QByteArray& data) {
    const QString path = dir.filePath(fn);
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
    f.close();
    return path;
}

} // namespace

class TestSaveFile : public QObject {
    Q_OBJECT
private slots:

    // A well-formed uncompressed SSE save parses every header field, the screenshot
    // dimensions/bytes, and the plugin list.
    void uncompressedSSE() {
        QTemporaryDir dir;
        const QByteArray save = buildSave(
            12, "Dovahkiin", 42, "Whiterun", "Day 5, 10:00", "NordRace",
            /*sex*/1, /*saveNum*/7, /*w*/4, /*h*/3, /*compType*/0,
            pluginBlock({"Skyrim.esm", "Update.esm", "SkyUI.esp"}));
        const QString path = writeTemp(dir, "auto.ess", save);

        const SaveHeader h = parseSaveHeader(path);
        QVERIFY(h.ok);
        QCOMPARE(h.version, 12u);
        QCOMPARE(h.characterName, QString("Dovahkiin"));
        QCOMPARE(h.level, 42u);
        QCOMPARE(h.location, QString("Whiterun"));
        QCOMPARE(h.gameDate, QString("Day 5, 10:00"));
        QCOMPARE(h.raceEditorId, QString("NordRace"));
        QCOMPARE(h.sex, quint16(1));
        QCOMPARE(h.saveNumber, 7u);
        QCOMPARE(h.screenshotWidth, 4u);
        QCOMPARE(h.screenshotHeight, 3u);
        QCOMPARE(h.screenshotBytesPerPixel, 4);
        QCOMPARE(h.screenshotRgb.size(), 4 * 3 * 4); // w*h*bpp
        QVERIFY(h.pluginsReadable);
        QCOMPARE(h.plugins, QStringList({"Skyrim.esm", "Update.esm", "SkyUI.esp"}));
    }

    // A Legacy-Edition save (version < 12): 3-byte screenshot pixels, no
    // compressionType field, plugin list still readable.
    void uncompressedLE() {
        QTemporaryDir dir;
        const QByteArray save = buildSave(
            9, "Ysolda", 5, "Riverwood", "Day 1", "ImperialRace",
            0, 1, /*w*/2, /*h*/2, /*compType*/0,
            pluginBlock({"Skyrim.esm"}));
        const QString path = writeTemp(dir, "le.ess", save);

        const SaveHeader h = parseSaveHeader(path);
        QVERIFY(h.ok);
        QCOMPARE(h.version, 9u);
        QCOMPARE(h.characterName, QString("Ysolda"));
        QCOMPARE(h.screenshotBytesPerPixel, 3);
        QCOMPARE(h.screenshotRgb.size(), 2 * 2 * 3);
        QVERIFY(h.pluginsReadable);
        QCOMPARE(h.plugins, QStringList({"Skyrim.esm"}));
    }

    // The screenshot bytes are extracted at the correct offset (verify content, not
    // just size) - proves the header-block offset math is right.
    void screenshotContent() {
        QTemporaryDir dir;
        const QByteArray save = buildSave(
            12, "A", 1, "B", "C", "D", 0, 1, 2, 2, 0,
            pluginBlock({"Skyrim.esm"}), /*shotFill*/'\x2b');
        const QString path = writeTemp(dir, "shot.ess", save);
        const SaveHeader h = parseSaveHeader(path);
        QVERIFY(h.ok);
        QCOMPARE(h.screenshotRgb.size(), 2 * 2 * 4);
        for (char c : h.screenshotRgb) QCOMPARE(c, '\x2b');
    }

    // missingPlugins flags exactly the save plugins absent from the load order,
    // case-insensitively; returns empty when the load order covers everything.
    void missingPluginDetection() {
        SaveHeader h;
        h.ok = true;
        h.pluginsReadable = true;
        h.plugins = {"Skyrim.esm", "SkyUI.esp", "MyCoolMod.esp"};

        // Load order (mixed case) is missing SkyUI + MyCoolMod.
        const QStringList lo = {"skyrim.esm", "Update.esm"};
        QCOMPARE(missingPlugins(h, lo),
                 QStringList({"SkyUI.esp", "MyCoolMod.esp"}));

        // Full coverage (case-insensitive) => nothing missing.
        const QStringList full = {"SKYRIM.ESM", "skyui.esp", "mycoolmod.ESP"};
        QVERIFY(missingPlugins(h, full).isEmpty());

        // An unreadable plugin list never reports missing plugins (can't know).
        h.pluginsReadable = false;
        QVERIFY(missingPlugins(h, {}).isEmpty());
    }

    // Malformed / truncated / garbage input returns {ok=false} and never crashes.
    void malformedInputs() {
        QTemporaryDir dir;

        // Empty file.
        QCOMPARE(parseSaveHeader(writeTemp(dir, "empty.ess", QByteArray())).ok, false);

        // Wrong magic.
        QCOMPARE(parseSaveHeader(
            writeTemp(dir, "bad.ess", QByteArray("NOT_A_SAVEGAME___________", 25))).ok, false);

        // Nonexistent path.
        QCOMPARE(parseSaveHeader(dir.filePath("nope.ess")).ok, false);

        // Right magic but truncated header (headerSize claims more than exists).
        {
            Builder b;
            b.raw(QByteArray("TESV_SAVEGAME", 13));
            b.u32(500);            // headerSize far beyond the file
            b.u32(12);             // a lone version field, then EOF
            QCOMPARE(parseSaveHeader(writeTemp(dir, "trunc.ess", b.b)).ok, false);
        }

        // Truncated mid-screenshot: valid header, but the pixel bytes are cut off.
        {
            QByteArray full = buildSave(12, "X", 1, "Y", "Z", "R", 0, 1, 8, 8, 0,
                                        pluginBlock({"Skyrim.esm"}));
            full.truncate(40); // chop inside the screenshot region
            QCOMPARE(parseSaveHeader(writeTemp(dir, "cut.ess", full)).ok, false);
        }

        // Random garbage of assorted lengths.
        for (int n : {1, 5, 13, 17, 33, 128}) {
            QByteArray g(n, '\xa5');
            const SaveHeader h = parseSaveHeader(writeTemp(dir, QString("g%1.ess").arg(n), g));
            QCOMPARE(h.ok, false); // no magic => rejected, no crash
        }
    }

    // A valid header whose plugin block is truncated: the header is still ok (its
    // guaranteed value survives), but the plugin list degrades to unreadable.
    void truncatedPluginBlock() {
        QTemporaryDir dir;
        QByteArray body = pluginBlock({"Skyrim.esm", "SkyUI.esp"});
        body.truncate(body.size() - 5); // cut into the last plugin string
        const QByteArray save = buildSave(12, "N", 3, "L", "D", "R", 0, 2, 2, 2, 0, body);
        const SaveHeader h = parseSaveHeader(writeTemp(dir, "pt.ess", save));
        QVERIFY(h.ok);                 // header + screenshot parsed
        QVERIFY(!h.pluginsReadable);   // plugin block didn't
        QVERIFY(h.plugins.isEmpty());
    }

    // The save-previewer summary formats every key field from the header.
    void summaryHtml_hasKeyFields() {
        SaveHeader h;
        h.ok = true;
        h.characterName = "Lydia";
        h.level = 42;
        h.location = "Whiterun";
        h.gameDate = "Day 5";
        h.raceEditorId = "NordRace";
        h.sex = 1; // female
        h.saveNumber = 7;
        h.version = 12;
        h.plugins = QStringList{"Skyrim.esm", "Update.esm", "SkyUI.esp"};
        h.pluginsReadable = true;
        const QString s = saveSummaryHtml(h, "06 Jul 2026, 12:00");
        QVERIFY(s.contains("Lydia"));
        QVERIFY(s.contains("42"));
        QVERIFY(s.contains("Whiterun"));
        QVERIFY(s.contains("Nord"));          // "Race" suffix trimmed
        QVERIFY(s.contains("Female"));
        QVERIFY(s.contains("06 Jul 2026, 12:00"));
        QVERIFY(s.contains("3"));             // plugin count
    }

    // An unreadable (compressed) plugin list is reported gracefully, not as a count.
    void summaryHtml_unreadablePluginsNoted() {
        SaveHeader h;
        h.ok = true;
        h.characterName = "Nemesis";
        h.pluginsReadable = false;
        h.compressionType = 2;
        const QString s = saveSummaryHtml(h, "now");
        QVERIFY(s.contains("Nemesis"));
        QVERIFY(s.toLower().contains("not readable") || s.toLower().contains("compressed"));
    }

#ifdef SOLERO_HAVE_ZLIB
    // A zlib-compressed SSE save (compressionType 1): the plugin list lives inside
    // the compressed body and is recovered via the linked zlib decompressor.
    void zlibCompressed() {
        QTemporaryDir dir;
        const QByteArray body = pluginBlock({"Skyrim.esm", "Dawnguard.esm", "SkyUI.esp"});

        // Compress the body with zlib and wrap it: u32 uncompressedLen, u32
        // compressedLen, then the compressed bytes (the SSE compressed-block layout).
        uLongf bound = compressBound(uLong(body.size()));
        QByteArray comp(int(bound), '\0');
        uLongf compLen = bound;
        QCOMPARE(compress(reinterpret_cast<Bytef*>(comp.data()), &compLen,
                          reinterpret_cast<const Bytef*>(body.constData()),
                          uLong(body.size())), Z_OK);
        comp.resize(int(compLen));

        Builder wrapper;
        wrapper.u32(quint32(body.size())); // uncompressedLen
        wrapper.u32(quint32(comp.size())); // compressedLen
        wrapper.raw(comp);

        const QByteArray save = buildSave(12, "Serana", 60, "Fort Dawnguard",
                                          "Day 30", "NordRace", 1, 9, 4, 4,
                                          /*compType*/1, wrapper.b);
        const SaveHeader h = parseSaveHeader(writeTemp(dir, "zc.ess", save));
        QVERIFY(h.ok);
        QCOMPARE(h.characterName, QString("Serana"));
        QCOMPARE(h.compressionType, quint16(1));
        QVERIFY(h.pluginsReadable);
        QCOMPARE(h.plugins, QStringList({"Skyrim.esm", "Dawnguard.esm", "SkyUI.esp"}));
    }

    // A compressed save whose body is corrupt: the header still parses (ok=true),
    // but decompression fails so the plugin list degrades to unreadable - no crash.
    void corruptCompressedDegrades() {
        QTemporaryDir dir;
        Builder wrapper;
        wrapper.u32(200);                              // claimed uncompressedLen
        wrapper.u32(16);                               // compressedLen
        wrapper.raw(QByteArray(16, '\xff'));           // not valid zlib data
        const QByteArray save = buildSave(12, "C", 1, "L", "D", "R", 0, 1, 2, 2,
                                          /*compType*/1, wrapper.b);
        const SaveHeader h = parseSaveHeader(writeTemp(dir, "cc.ess", save));
        QVERIFY(h.ok);
        QVERIFY(!h.pluginsReadable);
        QVERIFY(h.plugins.isEmpty());
    }
#endif
};

QTEST_MAIN(TestSaveFile)
#include "test_SaveFile.moc"
