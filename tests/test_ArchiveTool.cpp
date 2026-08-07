// Extraction has to land files where the mod expects them. An archive written on
// Windows can store a whole path as one entry name ("SKSE\Plugins\foo.dll"), and
// because backslash is a legal filename character here, every extractor happily
// makes a single file called that. Nothing errors; the mod just never works.
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>
#include "install/ArchiveTool.h"
using namespace solero;

namespace {

quint32 crc32Of(const QByteArray& data) {
    static quint32 table[256];
    static bool built = false;
    if (!built) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void put16(QByteArray& out, quint16 v) { out.append(char(v & 0xFF)); out.append(char(v >> 8)); }
void put32(QByteArray& out, quint32 v) {
    for (int i = 0; i < 4; ++i) out.append(char((v >> (8 * i)) & 0xFF));
}

// Minimal stored-mode (uncompressed) zip writer. Hand-rolled so the fixture can
// hold entry names no ordinary archiver would produce on this platform.
void writeZip(const QString& path, const QList<QPair<QByteArray, QByteArray>>& entries) {
    QByteArray local, central;
    for (const auto& e : entries) {
        const QByteArray& name = e.first;
        const QByteArray& body = e.second;
        const quint32 crc = crc32Of(body);
        const quint32 offset = quint32(local.size());

        local.append("PK\x03\x04", 4);
        put16(local, 20); put16(local, 0); put16(local, 0);   // version, flags, method=store
        put16(local, 0);  put16(local, 0);                     // time, date
        put32(local, crc); put32(local, quint32(body.size())); put32(local, quint32(body.size()));
        put16(local, quint16(name.size())); put16(local, 0);
        local.append(name);
        local.append(body);

        central.append("PK\x01\x02", 4);
        put16(central, 20); put16(central, 20); put16(central, 0); put16(central, 0);
        put16(central, 0);  put16(central, 0);
        put32(central, crc); put32(central, quint32(body.size())); put32(central, quint32(body.size()));
        put16(central, quint16(name.size()));
        put16(central, 0); put16(central, 0); put16(central, 0);
        put16(central, 0); put32(central, 0);
        put32(central, offset);
        central.append(name);
    }
    QByteArray out = local;
    const quint32 cdOffset = quint32(out.size());
    out.append(central);
    out.append("PK\x05\x06", 4);
    put16(out, 0); put16(out, 0);
    put16(out, quint16(entries.size())); put16(out, quint16(entries.size()));
    put32(out, quint32(central.size())); put32(out, cdOffset);
    put16(out, 0);

    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(out);
}

QStringList filesUnder(const QString& dir) {
    QStringList out;
    const QString norm = QDir::cleanPath(dir);
    QDirIterator it(norm, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next().mid(norm.length() + 1);
    out.sort();
    return out;
}

bool haveSevenZip() {
    for (const QString& c : {"7z", "7za", "7zr"})
        if (!QStandardPaths::findExecutable(c).isEmpty()) return true;
    return false;
}

} // namespace

class TestArchiveTool : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (!haveSevenZip()) QSKIP("7z is not on PATH");
    }

    // Nexus 33574 (Main Menu Randomizer SE) ships exactly this: SKSE never saw the
    // plugin because the "directory" was part of the filename.
    void expandsAWindowsPathStoredAsOneEntryName() {
        QTemporaryDir tmp;
        const QString zip  = tmp.path() + "/mod.zip";
        const QString dest = tmp.path() + "/out";
        writeZip(zip, {
            { "SKSE\\Plugins\\main_menu_randomizer.dll", "DLL" },
            { "SKSE\\Plugins\\MainMenuRandomizer.config.toml", "TOML" },
            { "readme.txt", "hello" },
        });

        QVERIFY(ArchiveTool::extract(zip, dest));
        QCOMPARE(filesUnder(dest), (QStringList{
            "SKSE/Plugins/MainMenuRandomizer.config.toml",
            "SKSE/Plugins/main_menu_randomizer.dll",
            "readme.txt",
        }));
        // And the content survived the move, not just the name.
        QFile f(dest + "/SKSE/Plugins/main_menu_randomizer.dll");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("DLL"));
    }

    // Mixed archives are the common real case: most entries are fine and one is not.
    void leavesOrdinaryEntriesUntouched() {
        QTemporaryDir tmp;
        const QString zip  = tmp.path() + "/mod.zip";
        const QString dest = tmp.path() + "/out";
        writeZip(zip, {
            { "Data/meshes/a.nif", "A" },
            { "Data\\textures\\b.dds", "B" },
        });

        QVERIFY(ArchiveTool::extract(zip, dest));
        QCOMPARE(filesUnder(dest),
                 (QStringList{"Data/meshes/a.nif", "Data/textures/b.dds"}));
    }

    // The same trick is how a Windows-separator entry escapes the extraction root,
    // and 7z does not treat it as a path at all, so it cannot refuse it either.
    void rejectsTraversalHiddenInAWindowsPath() {
        QTemporaryDir tmp;
        const QString zip  = tmp.path() + "/evil.zip";
        const QString dest = tmp.path() + "/out";
        const QString outside = tmp.path() + "/pwned.txt";
        writeZip(zip, {
            { "..\\pwned.txt", "owned" },
            { "Data/ok.esp", "fine" },
        });

        QVERIFY2(!ArchiveTool::extract(zip, dest), "traversal entry extracted successfully");
        QVERIFY2(!QFile::exists(outside), "traversal entry escaped the extraction root");
    }

    // A directory entry stored with separators makes the whole subtree land under
    // one oddly named folder; its files still have to end up in the real tree.
    void expandsFilesInsideAWindowsNamedDirectory() {
        QTemporaryDir tmp;
        const QString zip  = tmp.path() + "/mod.zip";
        const QString dest = tmp.path() + "/out";
        writeZip(zip, { { "meshes\\actors/foo.nif", "N" } });

        QVERIFY(ArchiveTool::extract(zip, dest));
        QCOMPARE(filesUnder(dest), (QStringList{"meshes/actors/foo.nif"}));
        QVERIFY2(!QDir(dest).entryList(QDir::Dirs | QDir::NoDotAndDotDot).contains("meshes\\actors"),
                 "left an empty backslash-named directory behind");
    }
};

QTEST_MAIN(TestArchiveTool)
#include "test_ArchiveTool.moc"
