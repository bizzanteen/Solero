#include <QtTest>
#include <QTemporaryDir>
#include "tools/ExeResolve.h"

using namespace solero;

class TestExeResolve : public QObject {
    Q_OBJECT
private:
    static void touch(const QString& path) {
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
        f.write("x");
    }

private slots:
    void exactMatch() {
        QTemporaryDir d;
        touch(d.path() + "/ESLifier.exe");
        QCOMPARE(resolveToolExe(d.path(), "ESLifier.exe"), d.path() + "/ESLifier.exe");
    }
    void caseInsensitiveShallow() {
        QTemporaryDir d;
        touch(d.path() + "/eslifier.EXE");
        QCOMPARE(resolveToolExe(d.path(), "ESLifier.exe"), d.path() + "/eslifier.EXE");
    }
    void nestedMatch() {
        QTemporaryDir d;
        touch(d.path() + "/app/bin/Tool.exe");
        QCOMPARE(resolveToolExe(d.path(), "Tool.exe"), d.path() + "/app/bin/Tool.exe");
    }
    void relPathWithDirExactMatch() {
        QTemporaryDir d;
        touch(d.path() + "/bsarch/BSArch.exe");
        QCOMPARE(resolveToolExe(d.path(), "bsarch/BSArch.exe"), d.path() + "/bsarch/BSArch.exe");
    }
    void missingExeReturnsEmpty() {
        QTemporaryDir d;
        touch(d.path() + "/shadowman_hert.esp"); // wrong-content archive: no exe at all
        QCOMPARE(resolveToolExe(d.path(), "ESLifier.exe"), QString());
    }
    void emptyRelPathReturnsEmpty() {
        QTemporaryDir d;
        QCOMPARE(resolveToolExe(d.path(), ""), QString());
    }
};

QTEST_GUILESS_MAIN(TestExeResolve)
#include "test_ExeResolve.moc"
