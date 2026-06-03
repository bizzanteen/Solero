#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "deploy/Linker.h"
using namespace solero;

class TestLinker : public QObject {
    Q_OBJECT
private slots:
    void copyFile_createsTarget() {
        QTemporaryDir tmp;
        QString src = tmp.path() + "/src/foo.txt";
        QString dst = tmp.path() + "/dst/foo.txt";
        QDir().mkpath(tmp.path() + "/src");
        QFile f(src); f.open(QIODevice::WriteOnly); f.write("hello"); f.close();

        Linker linker(DeployMode::Copy);
        QVERIFY(linker.deploy(src, dst));
        QVERIFY(QFile::exists(dst));
        QFile r(dst); r.open(QIODevice::ReadOnly);
        QCOMPARE(r.readAll(), QByteArray("hello"));
    }
    void symlink_createsLink() {
        QTemporaryDir tmp;
        QString src = tmp.path() + "/src/bar.txt";
        QString dst = tmp.path() + "/dst/bar.txt";
        QDir().mkpath(tmp.path() + "/src");
        QFile f(src); f.open(QIODevice::WriteOnly); f.write("world"); f.close();

        Linker linker(DeployMode::SymLink);
        QVERIFY(linker.deploy(src, dst));
        QVERIFY(QFileInfo(dst).isSymLink());
    }
    void removeFile_deletesTarget() {
        QTemporaryDir tmp;
        QString path = tmp.path() + "/todelete.txt";
        QFile f(path); f.open(QIODevice::WriteOnly); f.write("x"); f.close();

        Linker linker(DeployMode::Copy);
        QVERIFY(linker.remove(path));
        QVERIFY(!QFile::exists(path));
    }
    void deploy_createsParentDirs() {
        QTemporaryDir tmp;
        QString src = tmp.path() + "/src/deep/nested/file.nif";
        QString dst = tmp.path() + "/dst/deep/nested/file.nif";
        QDir().mkpath(tmp.path() + "/src/deep/nested");
        QFile f(src); f.open(QIODevice::WriteOnly); f.write("mesh"); f.close();

        Linker linker(DeployMode::Copy);
        QVERIFY(linker.deploy(src, dst));
        QVERIFY(QFile::exists(dst));
    }
};
QTEST_MAIN(TestLinker)
#include "test_Linker.moc"
