#include <QtTest>
#include <QSet>
#include <QTemporaryFile>
#include "patch/PatchScanner.h"
#include "fomod/FomodEngine.h"
#include "fomod/FomodTypes.h"
using namespace solero;

// helpers

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

// Run the pure core, wiring collect() to a FomodEngine over the same module so
// <conditionalFileInstalls> is evaluated exactly as production does.
static QList<PatchCandidate> run(const FomodModule& m,
                                 const FomodEngine::Selection& orig,
                                 const FilePresentFn& present,
                                 const AlreadyInstalledFn& installed,
                                 const QList<InstalledModId>& mods,
                                 const PatchModMeta& meta) {
    FomodEngine engine;
    engine.setModule(m);
    engine.setFilePresent(present);
    CollectFn collect = [&engine](const FomodEngine::Selection& s){ return engine.collectFiles(s); };
    return findPatches(m, orig, present, installed, mods, collect, meta);
}

static InstalledModId installedMod(const QString& name, const QStringList& plugins = {}) {
    InstalledModId id;
    id.modId = name; id.name = name; id.nexusName = name;
    id.pluginBasenames = plugins;
    id.normalizedName = normalizeName(name);
    return id;
}

class TestPatchScanner : public QObject {
    Q_OBJECT
    PatchModMeta meta{ "mod-1", "Test Mod", "/tmp/test.7z", true };

    static FilePresentFn presentSet(const QSet<QString>& present) {
        return [present](const QString& f) { return present.contains(f); };
    }
    static AlreadyInstalledFn installedSet(const QSet<QString>& installed) {
        return [installed](const FomodFile& f) { return installed.contains(f.destination); };
    }
    // Folder/file "already installed" by normalized destination path.
    static AlreadyInstalledFn installedDests(const QSet<QString>& dests) {
        return [dests](const FomodFile& f) {
            return dests.contains(normalizePath(f.destination));
        };
    }

private slots:
    // Retained MVP cases (direct-file option gated by a typeDescriptor)

    // (i) conditioned on a PRESENT plugin, dest not installed -> candidate.
    void applicableNotInstalled_isCandidate() {
        auto m = makeModule("SkyUI Patch", "patch.esp",
                            depTypeXml("SkyUI_SE.esp", "Active", "Optional"));
        auto cands = run(m, {}, presentSet({"SkyUI_SE.esp"}), installedSet({}), {}, meta);
        QCOMPARE(cands.size(), 1);
        QCOMPARE(cands[0].optionName, QString("SkyUI Patch"));
        QVERIFY(cands[0].reason.contains("SkyUI_SE.esp"));
        QCOMPARE(cands[0].modId, QString("mod-1"));
    }

    // (ii) conditioned on an ABSENT plugin -> not a candidate.
    void absentDependency_notCandidate() {
        auto m = makeModule("Other Patch", "patch.esp",
                            depTypeXml("Other.esp", "Active", "NotUsable"));
        auto cands = run(m, {}, presentSet({"SkyUI_SE.esp"}), installedSet({}), {}, meta);
        QCOMPARE(cands.size(), 0);
    }

    // (iii) applicable but dest files ALREADY installed -> not a candidate.
    void applicableAlreadyInstalled_notCandidate() {
        auto m = makeModule("SkyUI Patch", "patch.esp",
                            depTypeXml("SkyUI_SE.esp", "Active", "Optional"));
        auto cands = run(m, {}, presentSet({"SkyUI_SE.esp"}),
                         installedSet({"patch.esp"}), {}, meta);
        QCOMPARE(cands.size(), 0);
    }

    // (iv) unconditional option -> not a candidate (not a "patch").
    void unconditional_notCandidate() {
        auto m = makeModule("Core Files", "core.esp", QString());
        auto cands = run(m, {}, presentSet({"SkyUI_SE.esp"}), installedSet({}), {}, meta);
        QCOMPARE(cands.size(), 0);
    }

    // Grass FPS Booster: flag-setting options + conditionalFileInstalls

    void grassFpsBooster_flagDriven() {
        // A real "pick which mod you have" module: a quality step (sets Quality/
        // Performance), a grass step whose options only set flags (no files), and
        // all payloads in <conditionalFileInstalls> gated by And(Quality, <grass>).
        const QString xml =
            "<config><moduleName>Grass FPS Booster</moduleName><installSteps>"
            "<installStep name=\"Quality Level\"><optionalFileGroups>"
            "<group name=\"Quality\" type=\"SelectExactlyOne\"><plugins>"
            "<plugin name=\"Quality\"><description>High</description>"
            "<conditionFlags><flag name=\"Quality\">On</flag></conditionFlags>"
            "<typeDescriptor><type name=\"Optional\"/></typeDescriptor></plugin>"
            "<plugin name=\"Performance\"><description>Fast</description>"
            "<conditionFlags><flag name=\"Performance\">On</flag></conditionFlags>"
            "<typeDescriptor><type name=\"Optional\"/></typeDescriptor></plugin>"
            "</plugins></group></optionalFileGroups></installStep>"
            "<installStep name=\"Grass Mods\"><optionalFileGroups>"
            "<group name=\"Grass\" type=\"SelectAny\"><plugins>"
            "<plugin name=\"Folkvangr\"><description>Folkvangr</description>"
            "<conditionFlags><flag name=\"Folkvangr\">On</flag></conditionFlags>"
            "<typeDescriptor><type name=\"Optional\"/></typeDescriptor></plugin>"
            "<plugin name=\"Tamrielic\"><description>Tamrielic Grass</description>"
            "<conditionFlags><flag name=\"Tamrielic\">On</flag></conditionFlags>"
            "<typeDescriptor><type name=\"Optional\"/></typeDescriptor></plugin>"
            "<plugin name=\"Cathedral\"><description>Cathedral</description>"
            "<conditionFlags><flag name=\"Cathedral\">On</flag></conditionFlags>"
            "<typeDescriptor><type name=\"Optional\"/></typeDescriptor></plugin>"
            "</plugins></group></optionalFileGroups></installStep></installSteps>"
            "<conditionalFileInstalls><patterns>"
            "<pattern><dependencies operator=\"And\">"
            "<flagDependency flag=\"Quality\" value=\"On\"/>"
            "<flagDependency flag=\"Folkvangr\" value=\"On\"/></dependencies>"
            "<files><folder source=\"Quality\\Folkvangr\" destination=\"meshes/grass/folkvangr\"/></files></pattern>"
            "<pattern><dependencies operator=\"And\">"
            "<flagDependency flag=\"Quality\" value=\"On\"/>"
            "<flagDependency flag=\"Tamrielic\" value=\"On\"/></dependencies>"
            "<files><folder source=\"Quality\\Tamrielic\" destination=\"meshes/grass/tamrielic\"/></files></pattern>"
            "<pattern><dependencies operator=\"And\">"
            "<flagDependency flag=\"Quality\" value=\"On\"/>"
            "<flagDependency flag=\"Cathedral\" value=\"On\"/></dependencies>"
            "<files><folder source=\"Quality\\Cathedral\" destination=\"meshes/grass/cathedral\"/></files></pattern>"
            "</patterns></conditionalFileInstalls></config>";

        QTemporaryFile tf;
        QVERIFY(tf.open());
        tf.write(xml.toUtf8());
        tf.flush();
        FomodEngine engine;
        QVERIFY(engine.load(tf.fileName()));
        const FomodModule m = engine.module();

        // Original install: Quality level + Folkvangr already chosen (so the
        // Folkvangr payload is already on disk). Tamrielic was not chosen.
        FomodEngine::Selection orig;
        orig.insert(FomodEngine::selKey(0, 0, 0), true); // Quality
        orig.insert(FomodEngine::selKey(1, 0, 0), true); // Folkvangr

        // Load order now has both grass mods present, but only the Folkvangr GFB
        // payload is installed.
        const QList<InstalledModId> mods = {
            installedMod("Folkvangr - Grass and Landscape Overhaul",
                         {"Folkvangr - Grass and Landscape Overhaul.esp"}),
            installedMod("Tamrielic Grass", {"TamrielicGrass.esp"}),
        };
        auto installed = installedDests({ "meshes/grass/folkvangr" });
        // flagDependency-only module: filePresent is irrelevant here.
        auto present = presentSet({});

        auto cands = run(m, orig, present, installed, mods,
                         { "gfb", "Grass FPS Booster", "/tmp/gfb.7z", true });

        // Exactly one candidate: the Tamrielic patch (flag-driven).
        QCOMPARE(cands.size(), 1);
        QCOMPARE(cands[0].optionName, QString("Tamrielic"));
        QVERIFY(cands[0].reason.contains("Tamrielic Grass"));
        QCOMPARE(cands[0].files.size(), 1);
        QCOMPARE(cands[0].files[0].destination, QString("meshes/grass/tamrielic"));
        QVERIFY(cands[0].files[0].isFolder);
        // Folkvangr (already installed) and Cathedral (mod absent) are not surfaced.
        for (const auto& c : cands) {
            QVERIFY(c.optionName != QString("Folkvangr"));
            QVERIFY(c.optionName != QString("Cathedral"));
        }
    }

    // A grass option whose flag maps to a present mod but whose payload is ALREADY
    // installed must not be re-surfaced (here: install both, expect nothing).
    void grassFpsBooster_allInstalled_noCandidates() {
        const QString xml =
            "<config><moduleName>GFB</moduleName><installSteps>"
            "<installStep name=\"Q\"><optionalFileGroups>"
            "<group name=\"Q\" type=\"SelectExactlyOne\"><plugins>"
            "<plugin name=\"Quality\"><description>q</description>"
            "<conditionFlags><flag name=\"Quality\">On</flag></conditionFlags>"
            "<typeDescriptor><type name=\"Optional\"/></typeDescriptor></plugin>"
            "</plugins></group></optionalFileGroups></installStep>"
            "<installStep name=\"G\"><optionalFileGroups>"
            "<group name=\"G\" type=\"SelectAny\"><plugins>"
            "<plugin name=\"Tamrielic\"><description>t</description>"
            "<conditionFlags><flag name=\"Tamrielic\">On</flag></conditionFlags>"
            "<typeDescriptor><type name=\"Optional\"/></typeDescriptor></plugin>"
            "</plugins></group></optionalFileGroups></installStep></installSteps>"
            "<conditionalFileInstalls><patterns><pattern>"
            "<dependencies operator=\"And\">"
            "<flagDependency flag=\"Quality\" value=\"On\"/>"
            "<flagDependency flag=\"Tamrielic\" value=\"On\"/></dependencies>"
            "<files><folder source=\"Q\\Tamrielic\" destination=\"meshes/grass/tamrielic\"/></files>"
            "</pattern></patterns></conditionalFileInstalls></config>";
        QTemporaryFile tf; QVERIFY(tf.open()); tf.write(xml.toUtf8()); tf.flush();
        FomodEngine engine; QVERIFY(engine.load(tf.fileName()));
        FomodEngine::Selection orig;
        orig.insert(FomodEngine::selKey(0, 0, 0), true); // Quality only
        const QList<InstalledModId> mods = { installedMod("Tamrielic Grass") };
        auto installed = installedDests({ "meshes/grass/tamrielic" }); // already there
        auto cands = run(engine.module(), orig, presentSet({}), installed, mods,
                         { "gfb", "GFB", "/tmp/gfb.7z", true });
        QCOMPARE(cands.size(), 0);
    }

    // Non-installable mods (no source archive) are still detected but flagged.
    void detectedButNotInstallable() {
        auto m = makeModule("SkyUI Patch", "patch.esp",
                            depTypeXml("SkyUI_SE.esp", "Active", "Optional"));
        PatchModMeta noArchive{ "mod-2", "Imported Mod", QString(), false };
        auto cands = run(m, {}, presentSet({"SkyUI_SE.esp"}), installedSet({}), {}, noArchive);
        QCOMPARE(cands.size(), 1);
        QVERIFY(!cands[0].installable);
        QVERIFY(cands[0].sourceArchive.isEmpty());
    }

    // File-driven (conditionalFileInstalls) candidates are named by the patch
    // plugin they install and list the present trigger(s) - not a generic label.
    void fileDriven_namedByPayloadAndTriggers() {
        FomodModule m; m.moduleName = "Test"; m.valid = true;
        m.conditionalInstallsXml =
            "<conditionalFileInstalls><patterns><pattern>"
            "<dependencies operator=\"And\">"
            "<fileDependency file=\"JKs Skyrim.esp\" state=\"Active\"/>"
            "<fileDependency file=\"Northern Roads.esp\" state=\"Active\"/>"
            "</dependencies>"
            "<files><file source=\"opt/JK-NR Patch.esp\" destination=\"JK-NR Patch.esp\"/></files>"
            "</pattern></patterns></conditionalFileInstalls>";
        auto cands = run(m, {}, presentSet({"JKs Skyrim.esp", "Northern Roads.esp"}),
                         installedSet({}), {}, meta);
        QCOMPARE(cands.size(), 1);
        QCOMPARE(cands[0].optionName, QString("JK-NR Patch.esp"));   // named by payload
        QVERIFY(cands[0].reason.contains("JKs Skyrim.esp"));         // both triggers listed
        QVERIFY(cands[0].reason.contains("Northern Roads.esp"));
        QVERIFY(cands[0].reason.contains("present"));
    }
};

QTEST_MAIN(TestPatchScanner)
#include "test_PatchScanner.moc"
