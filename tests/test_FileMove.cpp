#include <QtTest>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include "core/FileMove.h"
using namespace solero;

class TestFileMove : public QObject {
    Q_OBJECT
private:
    static void writeFile(const QString& path, const QByteArray& data) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }
private slots:
    void movesFilesPreservingStructure_andReturnsCount() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString src = tmp.filePath("overwrite");
        const QString dst = tmp.filePath("staging/mod/Data");
        writeFile(src + "/SKSE/Plugins/foo.dll", "a");
        writeFile(src + "/meshes/x.nif", "bb");
        writeFile(src + "/top.txt", "ccc");

        const int moved = moveTreeContents(src, dst);
        QCOMPARE(moved, 3);

        // Files landed at the right relative paths.
        QVERIFY(QFile::exists(dst + "/SKSE/Plugins/foo.dll"));
        QVERIFY(QFile::exists(dst + "/meshes/x.nif"));
        QVERIFY(QFile::exists(dst + "/top.txt"));

        // Source dir still exists but is now empty (no leftover files/subdirs).
        QVERIFY(QDir(src).exists());
        QDir s(src);
        QCOMPARE(s.entryList(QDir::Files | QDir::Hidden, QDir::NoSort).size(), 0);
        QCOMPARE(s.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort).size(), 0);
    }

    void overwritesExistingDestFile() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString src = tmp.filePath("ow");
        const QString dst = tmp.filePath("data");
        writeFile(src + "/conflict.txt", "new");
        writeFile(dst + "/conflict.txt", "old");

        const int moved = moveTreeContents(src, dst);
        QCOMPARE(moved, 1);
        QFile f(dst + "/conflict.txt");
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("new"));
    }

    void missingSource_returnsZero() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QCOMPARE(moveTreeContents(tmp.filePath("does-not-exist"), tmp.filePath("dst")), 0);
    }
};
QTEST_MAIN(TestFileMove)
#include "test_FileMove.moc"
