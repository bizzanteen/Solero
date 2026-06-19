#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include "core/FileUtil.h"

using namespace solero;

namespace {
// Write `content` to a file at `path`, creating parent dirs.
void writeFile(const QString& path, const QByteArray& content = "x") {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
    f.close();
}
} // namespace

// dirHasFiles backs outputModHasStagedFiles: it gates the PGPatcher/Radium
// output-mod disable+redeploy dance (skip the whole thing when the output mod's
// staging Data/ has produced nothing yet).
class TestDirHasFiles : public QObject {
    Q_OBJECT
private slots:

    void missingDir_isFalse() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(!dirHasFiles(tmp.path() + "/does-not-exist"));
    }

    void emptyString_isFalse() {
        QVERIFY(!dirHasFiles(QString()));
    }

    void existingButEmptyDir_isFalse() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = tmp.path() + "/Data";
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(!dirHasFiles(dir));
    }

    void dirWithOnlySubdirs_isFalse() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = tmp.path() + "/Data";
        QVERIFY(QDir().mkpath(dir + "/textures/armor")); // dirs only, no files
        QVERIFY(!dirHasFiles(dir));
    }

    void fileAtRoot_isTrue() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = tmp.path() + "/Data";
        writeFile(dir + "/skse.ini");
        QVERIFY(dirHasFiles(dir));
    }

    void fileDeepInSubtree_isTrue() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = tmp.path() + "/Data";
        writeFile(dir + "/textures/armor/iron.dds"); // nested file
        QVERIFY(dirHasFiles(dir));
    }
};

QTEST_MAIN(TestDirHasFiles)
#include "test_DirHasFiles.moc"
