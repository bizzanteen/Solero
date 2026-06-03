#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QFile>
#include <QDir>
#include "install/ModInstaller.h"
#include "install/ArchiveTool.h"
using namespace solero;

static void writeFile(const QString& path, const QByteArray& c = "x") {
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path); f.open(QIODevice::WriteOnly); f.write(c);
}

class TestModInstaller : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (!ArchiveTool::sevenZipAvailable()) QSKIP("7z not available");
    }
    void install_dataRelative_wrapsUnderData() {
        QTemporaryDir tmp;
        QString src = tmp.path() + "/srcmod";
        writeFile(src + "/textures/sky.dds", "tex");
        QString archive = tmp.path() + "/CoolTextures.zip";
        QProcess z; z.setWorkingDirectory(src);
        z.start("7z", {"a", archive, "."});
        QVERIFY(z.waitForFinished(60000));

        QString staging = tmp.path() + "/staging";
        auto r = ModInstaller::installArchive(archive, staging);
        QVERIFY2(r.success, r.errorMessage.toUtf8());
        QCOMPARE(r.modName, QString("CoolTextures"));
        QVERIFY(QFile::exists(staging + "/" + r.modId + "/Data/textures/sky.dds"));
    }
    void install_gameRoot_noWrap() {
        QTemporaryDir tmp;
        QString src = tmp.path() + "/srcmod";
        writeFile(src + "/skse64_loader.exe", "exe");
        writeFile(src + "/Data/Scripts/a.pex", "pex");
        QString archive = tmp.path() + "/SKSE.7z";
        QProcess z; z.setWorkingDirectory(src);
        z.start("7z", {"a", archive, "."});
        QVERIFY(z.waitForFinished(60000));

        QString staging = tmp.path() + "/staging";
        auto r = ModInstaller::installArchive(archive, staging);
        QVERIFY(r.success);
        QVERIFY(QFile::exists(staging + "/" + r.modId + "/skse64_loader.exe"));
        QVERIFY(QFile::exists(staging + "/" + r.modId + "/Data/Scripts/a.pex"));
    }
};
QTEST_MAIN(TestModInstaller)
#include "test_ModInstaller.moc"
