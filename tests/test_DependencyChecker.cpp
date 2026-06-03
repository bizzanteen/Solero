#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "install/DependencyChecker.h"
#include "core/ModList.h"
using namespace solero;
static void touch(const QString& p){ QDir().mkpath(QFileInfo(p).path()); QFile f(p); f.open(QIODevice::WriteOnly); f.write("x"); }

class TestDependencyChecker : public QObject { Q_OBJECT
private slots:
    void flagsMissingSkseAndAddressLib() {
        QTemporaryDir tmp; QString staging = tmp.path();
        // A mod that ships an SKSE plugin dll, but no SKSE / Address Library present.
        touch(staging + "/modA/Data/SKSE/Plugins/cool.dll");
        ModList list;
        ModEntry a; a.type=EntryType::Mod; a.id="modA"; a.name="Cool SKSE Mod"; a.enabled=true;
        list.append(a);
        auto warns = DependencyChecker::check(list, staging);
        QVERIFY(warns.contains("modA"));
        QVERIFY(warns["modA"].join(" ").contains("SKSE", Qt::CaseInsensitive));
        QVERIFY(warns["modA"].join(" ").contains("Address Library", Qt::CaseInsensitive));
    }
    void satisfiedWhenDepsPresent() {
        QTemporaryDir tmp; QString staging = tmp.path();
        touch(staging + "/modA/Data/SKSE/Plugins/cool.dll");
        touch(staging + "/skse/skse64_loader.exe");
        touch(staging + "/addrlib/Data/SKSE/Plugins/versionlib-1-6-1170-0.bin");
        ModList list;
        for (auto id : {QString("modA"), QString("skse"), QString("addrlib")}) {
            ModEntry m; m.type=EntryType::Mod; m.id=id; m.name=id; m.enabled=true; list.append(m);
        }
        auto warns = DependencyChecker::check(list, staging);
        QVERIFY(!warns.contains("modA")); // deps satisfied
    }
    void disabledDepDoesNotCount() {
        QTemporaryDir tmp; QString staging = tmp.path();
        touch(staging + "/modA/Data/SKSE/Plugins/cool.dll");
        touch(staging + "/skse/skse64_loader.exe");
        touch(staging + "/addrlib/Data/SKSE/Plugins/versionlib-1-6-1170-0.bin");
        ModList list;
        ModEntry a; a.type=EntryType::Mod; a.id="modA"; a.name="A"; a.enabled=true; list.append(a);
        ModEntry s; s.type=EntryType::Mod; s.id="skse"; s.name="S"; s.enabled=false; list.append(s);   // disabled
        ModEntry b; b.type=EntryType::Mod; b.id="addrlib"; b.name="B"; b.enabled=true; list.append(b);
        auto warns = DependencyChecker::check(list, staging);
        QVERIFY(warns.contains("modA"));
        QVERIFY(warns["modA"].join(" ").contains("SKSE", Qt::CaseInsensitive));
    }
};
QTEST_MAIN(TestDependencyChecker)
#include "test_DependencyChecker.moc"
