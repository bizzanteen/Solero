#include <QtTest>
#include <functional>
#include "fomod/FomodScanner.h"
#include "fomod/FomodTypes.h"
using namespace solero;

// tiny builders for hand-constructing a FomodModule
static FomodFile file(const QString& src, const QString& dest) {
    FomodFile f; f.source = src; f.destination = dest; f.isFolder = false; return f;
}
static FomodOption opt(const QString& name, const QList<FomodFile>& files,
                       const QList<FomodFlag>& flags = {}) {
    FomodOption o; o.name = name; o.files = files; o.flags = flags; return o;
}
static FomodGroup group(GroupType t, const QList<FomodOption>& opts) {
    FomodGroup g; g.name = "G"; g.type = t; g.options = opts; return g;
}
static FomodModule oneStep(const QList<FomodGroup>& groups) {
    FomodModule m; FomodStep s; s.name = "Main"; s.groups = groups;
    m.steps << s; m.valid = true; return m;
}

// Marks an archive entry as carrying the fomod config so the wrapper-prefix
// detection in reconstructSelection treats sources as root-relative.
static QList<FomodArchiveEntry> withConfig(QList<FomodArchiveEntry> e) {
    e.prepend({ "fomod/ModuleConfig.xml", 0 });
    return e;
}

class TestFomodScanner : public QObject {
    Q_OBJECT
private slots:
    // (i) SelectAny patch-hub: only options whose unique files are present win.
    void anyUniqueFilePresence() {
        FomodModule m = oneStep({ group(GroupType::Any, {
            opt("A", { file("patches/a.esp", "a.esp") }),
            opt("B", { file("patches/b.esp", "b.esp") }),
            opt("C", { file("patches/c.esp", "c.esp") }),
        }) });
        const auto entries = withConfig({
            { "patches/a.esp", 0x111 },
            { "patches/b.esp", 0x222 },
            { "patches/c.esp", 0x333 },
        });
        QSet<QString> installed{ "a.esp", "c.esp" }; // b.esp absent
        auto noCrc = std::function<quint32(const QString&)>([](const QString&){ return 0u; });

        const ReconstructResult r = reconstructSelection(m, entries, installed, noCrc);
        QCOMPARE(r.steps.size(), 1);
        const QStringList& sel = r.steps[0].selected;
        QCOMPARE(sel.size(), 2);
        QVERIFY(sel.contains("A"));
        QVERIFY(sel.contains("C"));
        QVERIFY(!sel.contains("B"));
    }

    // (ii) SelectExactlyOne where B's files are a subset of A's: A wins via its
    // unique file (y.esp); B is not selected.
    void exactlyOneSubsetPrefersSuperset() {
        FomodModule m = oneStep({ group(GroupType::ExactlyOne, {
            opt("Full", { file("full/x.esp", "x.esp"), file("full/y.esp", "y.esp") }),
            opt("Lite", { file("lite/x.esp", "x.esp") }),
        }) });
        const auto entries = withConfig({
            { "full/x.esp", 0x1 },
            { "full/y.esp", 0x2 },
            { "lite/x.esp", 0x3 },
        });
        QSet<QString> installed{ "x.esp", "y.esp" };
        auto noCrc = std::function<quint32(const QString&)>([](const QString&){ return 0u; });

        const ReconstructResult r = reconstructSelection(m, entries, installed, noCrc);
        QCOMPARE(r.steps[0].selected, QStringList{ "Full" });
    }

    // (iii) Same-path variant group (identical destinations, different CRC):
    // disambiguate by the on-disk CRC, which matches option "1k".
    void samePathVariantResolvedByCrc() {
        FomodModule m = oneStep({ group(GroupType::ExactlyOne, {
            opt("1k", { file("1k/tex.dds", "tex.dds") }),
            opt("2k", { file("2k/tex.dds", "tex.dds") }),
        }) });
        const auto entries = withConfig({
            { "1k/tex.dds", 0xAAAA },
            { "2k/tex.dds", 0xBBBB },
        });
        QSet<QString> installed{ "tex.dds" };
        auto crc = std::function<quint32(const QString&)>([](const QString& p) -> quint32 {
            return p == "tex.dds" ? 0xAAAAu : 0u;
        });

        const ReconstructResult r = reconstructSelection(m, entries, installed, crc);
        QCOMPARE(r.steps[0].selected, QStringList{ "1k" });
        QVERIFY(r.ambiguous); // CRC fallback was used
    }

    // (iv) Flag-driven module (file-less options + flags + conditionalFileInstalls)
    // is classified needs-rerun and no choices are fabricated.
    void flagDrivenClassifiedNeedsRerun() {
        FomodModule m = oneStep({ group(GroupType::ExactlyOne, {
            opt("Has Mod X", {}, { FomodFlag{ "modx", "on" } }),
            opt("Has Mod Y", {}, { FomodFlag{ "mody", "on" } }),
        }) });
        m.conditionalInstallsXml =
            "<conditionalFileInstalls><patterns/></conditionalFileInstalls>";

        QCOMPARE(classifyModule(m), FomodClass::FlagDriven);

        // A direct-file module (options carry files) classifies the other way.
        FomodModule df = oneStep({ group(GroupType::Any, {
            opt("A", { file("a/a.esp", "a.esp") }),
        }) });
        QCOMPARE(classifyModule(df), FomodClass::DirectFile);
    }
};

QTEST_APPLESS_MAIN(TestFomodScanner)
#include "test_FomodScanner.moc"
