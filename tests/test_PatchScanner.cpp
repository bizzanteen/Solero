#include <QtTest>
#include <QSet>
#include "patch/PatchScanner.h"
#include "fomod/FomodTypes.h"
using namespace solero;

// Build a single-step, single-group module whose one option (a) has files, and
// (b) carries the given conditional <dependencyType> XML (empty => unconditional).
static FomodModule makeModule(const QString& optName,
                              const QString& destFile,
                              const QString& conditionTypeXml,
                              OptionType baseType = OptionType::Optional) {
    FomodFile f; f.source = "patch/" + destFile; f.destination = destFile;
    FomodOption opt;
    opt.name = optName;
    opt.description = "A patch option";
    opt.baseType = baseType;
    opt.files = { f };
    opt.conditionTypeXml = conditionTypeXml;
    FomodGroup g; g.name = "Patches"; g.type = GroupType::Any; g.options = { opt };
    FomodStep s; s.name = "Options"; s.groups = { g };
    FomodModule m; m.moduleName = "Test"; m.steps = { s }; m.valid = true;
    return m;
}

// <dependencyType>: if `file` is in the requested state -> Recommended, else
// fall back to defaultType.
static QString depTypeXml(const QString& file, const QString& state,
                          const QString& defaultType) {
    return QString(
        "<dependencyType><defaultType name=\"%1\"/>"
        "<patterns><pattern><dependencies>"
        "<fileDependency file=\"%2\" state=\"%3\"/>"
        "</dependencies><type name=\"Recommended\"/></pattern></patterns>"
        "</dependencyType>").arg(defaultType, file, state);
}

class TestPatchScanner : public QObject {
    Q_OBJECT
    PatchModMeta meta{ "mod-1", "Test Mod", "/tmp/test.7z" };

    static FilePresentFn presentSet(const QSet<QString>& present) {
        return [present](const QString& f) { return present.contains(f); };
    }
    static AlreadyInstalledFn installedSet(const QSet<QString>& installed) {
        return [installed](const FomodFile& f) { return installed.contains(f.destination); };
    }

private slots:
    // (i) conditioned on a PRESENT plugin, dest not installed -> candidate.
    void applicableNotInstalled_isCandidate() {
        auto m = makeModule("SkyUI Patch", "patch.esp",
                            depTypeXml("SkyUI_SE.esp", "Active", "Optional"));
        auto cands = candidatesForModule(m, presentSet({"SkyUI_SE.esp"}),
                                         installedSet({}), meta);
        QCOMPARE(cands.size(), 1);
        QCOMPARE(cands[0].optionName, QString("SkyUI Patch"));
        QVERIFY(cands[0].reason.contains("SkyUI_SE.esp"));
        QCOMPARE(cands[0].modId, QString("mod-1"));
    }

    // (ii) conditioned on an ABSENT plugin -> not a candidate.
    void absentDependency_notCandidate() {
        auto m = makeModule("Other Patch", "patch.esp",
                            depTypeXml("Other.esp", "Active", "NotUsable"));
        auto cands = candidatesForModule(m, presentSet({"SkyUI_SE.esp"}),
                                         installedSet({}), meta);
        QCOMPARE(cands.size(), 0);
    }

    // (iii) applicable but dest files ALREADY installed -> not a candidate.
    void applicableAlreadyInstalled_notCandidate() {
        auto m = makeModule("SkyUI Patch", "patch.esp",
                            depTypeXml("SkyUI_SE.esp", "Active", "Optional"));
        auto cands = candidatesForModule(m, presentSet({"SkyUI_SE.esp"}),
                                         installedSet({"patch.esp"}), meta);
        QCOMPARE(cands.size(), 0);
    }

    // (iv) unconditional option -> not a candidate (not a "patch").
    void unconditional_notCandidate() {
        auto m = makeModule("Core Files", "core.esp", QString());
        auto cands = candidatesForModule(m, presentSet({"SkyUI_SE.esp"}),
                                         installedSet({}), meta);
        QCOMPARE(cands.size(), 0);
    }
};

QTEST_MAIN(TestPatchScanner)
#include "test_PatchScanner.moc"
