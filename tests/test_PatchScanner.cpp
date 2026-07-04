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
                                 const PatchModMeta& meta) {
    FomodEngine engine;
    engine.setModule(m);
    engine.setFilePresent(present);
    CollectFn collect = [&engine](const FomodEngine::Selection& s){ return engine.collectFiles(s); };
    return findPatches(m, orig, present, installed, collect, meta);
}

class TestPatchScanner : public QObject {
    Q_OBJECT
    PatchModMeta meta{ "mod-1", "Test Mod", "/tmp/test.7z", true, "/tmp/staging/Test Mod" };

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
    // Direct-file option gated by a typeDescriptor fileDependency

    // (i) conditioned on a PRESENT plugin, dest not installed -> candidate whose
    //     reason names the trigger plugin.
    void applicableNotInstalled_isCandidate() {
        auto m = makeModule("SkyUI Patch", "patch.esp",
                            depTypeXml("SkyUI_SE.esp", "Active", "Optional"));
        auto cands = run(m, {}, presentSet({"SkyUI_SE.esp"}), installedSet({}), meta);
        QCOMPARE(cands.size(), 1);
        QCOMPARE(cands[0].optionName, QString("SkyUI Patch"));
        QVERIFY(cands[0].reason.contains("SkyUI_SE.esp"));
        QCOMPARE(cands[0].modId, QString("mod-1"));
        QCOMPARE(cands[0].stagingDir, QString("/tmp/staging/Test Mod"));
    }

    // (ii) conditioned on an ABSENT plugin -> not a candidate.
    void absentDependency_notCandidate() {
        auto m = makeModule("Other Patch", "patch.esp",
                            depTypeXml("Other.esp", "Active", "NotUsable"));
        auto cands = run(m, {}, presentSet({"SkyUI_SE.esp"}), installedSet({}), meta);
        QCOMPARE(cands.size(), 0);
    }

    // (iii) applicable but dest files ALREADY installed -> not a candidate.
    void applicableAlreadyInstalled_notCandidate() {
        auto m = makeModule("SkyUI Patch", "patch.esp",
                            depTypeXml("SkyUI_SE.esp", "Active", "Optional"));
        auto cands = run(m, {}, presentSet({"SkyUI_SE.esp"}),
                         installedSet({"patch.esp"}), meta);
        QCOMPARE(cands.size(), 0);
    }

    // (iv) unconditional option -> not a candidate (not a "patch").
    void unconditional_notCandidate() {
        auto m = makeModule("Core Files", "core.esp", QString());
        auto cands = run(m, {}, presentSet({"SkyUI_SE.esp"}), installedSet({}), meta);
        QCOMPARE(cands.size(), 0);
    }

    // FLAG-DRIVEN pass removed: flag-gated conditionals are never surfaced

    // A "pick which mod you have" module whose grass options only set flags, with
    // payloads in <conditionalFileInstalls>. Tamrielic's mod is present but its
    // flag was never picked (no fileDependency trigger) -> not surfaced.
    void grassFpsBooster_flagGated_notSurfaced() {
        const QString xml =
            "<config><moduleName>Grass FPS Booster</moduleName><installSteps>"
            "<installStep name=\"Quality Level\"><optionalFileGroups>"
            "<group name=\"Quality\" type=\"SelectExactlyOne\"><plugins>"
            "<plugin name=\"Quality\"><description>High</description>"
            "<conditionFlags><flag name=\"Quality\">On</flag></conditionFlags>"
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
            "</patterns></conditionalFileInstalls></config>";
        QTemporaryFile tf; QVERIFY(tf.open()); tf.write(xml.toUtf8()); tf.flush();
        FomodEngine engine; QVERIFY(engine.load(tf.fileName()));
        const FomodModule m = engine.module();

        // Original install: Quality + Folkvangr chosen (Folkvangr payload on disk).
        FomodEngine::Selection orig;
        orig.insert(FomodEngine::selKey(0, 0, 0), true); // Quality
        orig.insert(FomodEngine::selKey(1, 0, 0), true); // Folkvangr

        auto installed = installedDests({ "meshes/grass/folkvangr" });
        auto cands = run(m, orig, presentSet({}), installed,
                         { "gfb", "Grass FPS Booster", "/tmp/gfb.7z", true, "/tmp/staging/gfb" });
        // No fileDependency trigger anywhere -> nothing surfaced.
        QCOMPARE(cands.size(), 0);
    }

    // file-DRIVEN (conditionalFileInstalls gated by a fileDependency)

    // File-driven candidate named by the patch plugin it installs, reason lists the
    // present trigger(s).
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
                         installedSet({}), meta);
        QCOMPARE(cands.size(), 1);
        QCOMPARE(cands[0].optionName, QString("JK-NR Patch.esp"));   // named by payload
        QVERIFY(cands[0].reason.contains("JKs Skyrim.esp"));         // both triggers listed
        QVERIFY(cands[0].reason.contains("Northern Roads.esp"));
        QVERIFY(cands[0].reason.contains("present"));
    }

    // A file-driven patch whose destination already exists on disk is suppressed.
    void fileDriven_alreadyInstalledDestination_suppressed() {
        FomodModule m; m.moduleName = "Test"; m.valid = true;
        m.conditionalInstallsXml =
            "<conditionalFileInstalls><patterns><pattern>"
            "<dependencies><fileDependency file=\"JKs Skyrim.esp\" state=\"Active\"/></dependencies>"
            "<files><file source=\"opt/JK-NR Patch.esp\" destination=\"JK-NR Patch.esp\"/></files>"
            "</pattern></patterns></conditionalFileInstalls>";
        auto cands = run(m, {}, presentSet({"JKs Skyrim.esp"}),
                         installedDests({"jk-nr patch.esp"}), meta);
        QCOMPARE(cands.size(), 0);
    }

    // A conditional payload gated only by a flagDependency (no nameable file
    // trigger) is suppressed even when it is collected into the baseline. This is
    // the Embers-XD false-positive class.
    void fileDriven_noNameableTrigger_suppressed() {
        const QString xml =
            "<config><moduleName>Embers</moduleName><installSteps>"
            "<installStep name=\"S\"><optionalFileGroups>"
            "<group name=\"G\" type=\"SelectExactlyOne\"><plugins>"
            "<plugin name=\"Embers HD\"><description>d</description>"
            "<conditionFlags><flag name=\"EmbersHD\">On</flag></conditionFlags>"
            "<typeDescriptor><type name=\"Optional\"/></typeDescriptor></plugin>"
            "</plugins></group></optionalFileGroups></installStep></installSteps>"
            "<conditionalFileInstalls><patterns><pattern>"
            "<dependencies><flagDependency flag=\"EmbersHD\" value=\"On\"/></dependencies>"
            "<files><folder source=\"assets\\core\" destination=\"meshes/embers\"/></files>"
            "</pattern></patterns></conditionalFileInstalls></config>";
        QTemporaryFile tf; QVERIFY(tf.open()); tf.write(xml.toUtf8()); tf.flush();
        FomodEngine engine; QVERIFY(engine.load(tf.fileName()));
        FomodEngine::Selection orig;
        orig.insert(FomodEngine::selKey(0, 0, 0), true); // sets EmbersHD flag
        // Payload not on disk, but gated only by a flag -> no nameable trigger.
        auto cands = run(engine.module(), orig, presentSet({}), installedSet({}),
                         { "e", "Embers", "/tmp/e.7z", true, "/tmp/staging/e" });
        QCOMPARE(cands.size(), 0);
    }

    // Non-installable mods (no source archive) are still detected but flagged.
    void detectedButNotInstallable() {
        auto m = makeModule("SkyUI Patch", "patch.esp",
                            depTypeXml("SkyUI_SE.esp", "Active", "Optional"));
        PatchModMeta noArchive{ "mod-2", "Imported Mod", QString(), false, QString() };
        auto cands = run(m, {}, presentSet({"SkyUI_SE.esp"}), installedSet({}), noArchive);
        QCOMPARE(cands.size(), 1);
        QVERIFY(!cands[0].installable);
        QVERIFY(cands[0].sourceArchive.isEmpty());
    }

    // Selection establishment: log vs reconstruction (spec item 6)

    // When a fomod-choices log exists it is used verbatim and reconstruction is
    // not invoked; when it is absent, the reconstruct fallback drives selection.
    void establishSelection_logUsed_reconstructNotInvoked() {
        auto m = makeModule("A", "a.esp", QString());
        m.steps[0].groups[0].options[0].name = "A";

        // A log selecting option "A" in step "Options".
        QTemporaryFile log; QVERIFY(log.open());
        log.write("{\"steps\":[{\"step\":\"Options\",\"selected\":[\"A\"]}]}");
        log.flush();

        bool reconstructCalled = false;
        auto reconstruct = [&]() -> FomodEngine::Selection {
            reconstructCalled = true; return {};
        };
        bool reconstructed = true;
        FomodEngine::Selection sel =
            establishSelection(m, log.fileName(), reconstruct, reconstructed);
        QVERIFY(!reconstructCalled);
        QVERIFY(!reconstructed);
        QVERIFY(sel.value(FomodEngine::selKey(0, 0, 0)));
    }

    void establishSelection_noLog_reconstructInvoked() {
        auto m = makeModule("A", "a.esp", QString());
        bool reconstructCalled = false;
        FomodEngine::Selection injected;
        injected.insert(FomodEngine::selKey(0, 0, 0), true);
        auto reconstruct = [&]() -> FomodEngine::Selection {
            reconstructCalled = true; return injected;
        };
        bool reconstructed = false;
        FomodEngine::Selection sel =
            establishSelection(m, "/no/such/log.json", reconstruct, reconstructed);
        QVERIFY(reconstructCalled);
        QVERIFY(reconstructed);
        QVERIFY(sel.value(FomodEngine::selKey(0, 0, 0)));
    }
};

QTEST_MAIN(TestPatchScanner)
#include "test_PatchScanner.moc"
