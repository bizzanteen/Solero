#include <QtTest>
#include <QTemporaryDir>
#include "core/HealthCheck.h"
#include "core/Profile.h"
#include "deploy/ConflictIndex.h"
using namespace solero;

class TestHealthCheck : public QObject { Q_OBJECT
private slots:
    // Pure per-category builders

    void missingMasters_onlyFlagsNonEmpty() {
        QList<QPair<QString, QStringList>> in;
        in.append(qMakePair(QString("Child.esp"), QStringList{"Missing.esm"}));
        in.append(qMakePair(QString("Ok.esp"), QStringList{})); // no missing -> no issue
        auto out = health::missingMasterIssues(in);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].severity, HealthSeverity::Error);
        QCOMPARE(out[0].category, HealthCategory::MissingMaster);
        QCOMPARE(out[0].targetPlugin, QString("Child.esp"));
        QVERIFY(out[0].detail.contains("Missing.esm"));
    }

    void dependencyIssues_errorWithModTarget() {
        QHash<QString, QStringList> warns{ {"modA", {"Requires SKSE64"}} };
        QHash<QString, QString> names{ {"modA", "Cool Mod"} };
        QHash<QString, QString> nexus{ {"modA", "12345"} };
        auto out = health::dependencyIssues(warns, names, nexus);
        QCOMPARE(out.size(), 1);
        // Missing SKSE / Address Library is a hard runtime failure -> Error.
        QCOMPARE(out[0].severity, HealthSeverity::Error);
        QCOMPARE(out[0].category, HealthCategory::MissingDependency);
        QCOMPARE(out[0].targetModId, QString("modA"));
        QVERIFY(out[0].title.contains("Cool Mod"));
        QVERIFY(out[0].detail.contains("SKSE64"));
        QVERIFY(out[0].fixHint.contains("12345")); // Nexus id surfaced in the hint
    }

    void conflicts_infoWhenAny_noneWhenZero() {
        QVERIFY(health::conflictIssues(0, {}).isEmpty());
        auto out = health::conflictIssues(3, {"ModA", "ModB"});
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].severity, HealthSeverity::Info);
        QCOMPARE(out[0].category, HealthCategory::Conflict);
        QVERIFY(out[0].title.contains("3"));
        QVERIFY(out[0].detail.contains("ModA"));
    }

    void deployWarning_onlyWhenNonEmpty() {
        // No warning, no failures -> silent.
        QVERIFY(health::deployWarningIssues("", false).isEmpty());
        QVERIFY(health::deployWarningIssues("   ", false).isEmpty());
        // Advisory warning (cross-filesystem, LOOT-skip) -> Warning severity.
        auto out = health::deployWarningIssues("cross-filesystem hardlink fell back to copy", false);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].severity, HealthSeverity::Warning);
        QCOMPARE(out[0].category, HealthCategory::DeployWarning);
        QVERIFY(out[0].detail.contains("cross-filesystem"));
    }

    void deployWarning_hardFailureIsError() {
        // Hard file-link failures -> Error severity regardless of warning string.
        auto out = health::deployWarningIssues("3 file(s) failed to deploy", true);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].severity, HealthSeverity::Error);
        QCOMPARE(out[0].category, HealthCategory::DeployWarning);
        // hadFailures with empty warning string still surfaces as an Error.
        auto silent = health::deployWarningIssues("", true);
        QCOMPARE(silent.size(), 1);
        QCOMPARE(silent[0].severity, HealthSeverity::Error);
    }

    void deployState_notDeployedVsDirtyVsClean() {
        QCOMPARE(health::deployStateIssues(/*deployed=*/false, false).size(), 1);
        QCOMPARE(health::deployStateIssues(true,  /*dirty=*/true).size(), 1);
        QVERIFY(health::deployStateIssues(true, false).isEmpty()); // clean -> silent
    }

    void pluginLimit_errorOverCap_warnNear_silentBelow() {
        QVERIFY(health::pluginLimitIssues(100, 0).isEmpty());
        QCOMPARE(health::pluginLimitIssues(252, 5).first().severity, HealthSeverity::Warning);
        QCOMPARE(health::pluginLimitIssues(300, 5).first().severity, HealthSeverity::Error);
    }

    // Spec thresholds: silent < 240; Warning in [240, 254); Error at/above 254.
    // 254 is the widely-cited full-plugin (non-ESL) limit; wording surfaces
    // "N of 254 full plugins active".
    void pluginLimit_spec240WarnAnd254Error() {
        QVERIFY(health::pluginLimitIssues(239, 0).isEmpty());            // just below -> silent
        {
            auto out = health::pluginLimitIssues(240, 0);               // warning boundary
            QCOMPARE(out.size(), 1);
            QCOMPARE(out.first().severity, HealthSeverity::Warning);
            QCOMPARE(out.first().category, HealthCategory::PluginLimit);
            QVERIFY(out.first().title.contains("254"));
            QVERIFY(out.first().title.contains("240"));
        }
        QCOMPARE(health::pluginLimitIssues(253, 0).first().severity, HealthSeverity::Warning);
        {
            auto out = health::pluginLimitIssues(254, 0);               // error boundary
            QCOMPARE(out.size(), 1);
            QCOMPARE(out.first().severity, HealthSeverity::Error);
            QVERIFY(out.first().title.contains("254"));
        }
    }

    void worstSeverity_picksHighest() {
        QCOMPARE(worstSeverity({}), -1);
        QList<HealthIssue> mixed;
        HealthIssue info; info.severity = HealthSeverity::Info; mixed << info;
        HealthIssue warn; warn.severity = HealthSeverity::Warning; mixed << warn;
        QCOMPARE(worstSeverity(mixed), int(HealthSeverity::Warning));
        HealthIssue err; err.severity = HealthSeverity::Error; mixed << err;
        QCOMPARE(worstSeverity(mixed), int(HealthSeverity::Error));
    }

    // Full aggregation over a synthetic Profile + ConflictIndex

    void collect_aggregatesEveryCategory_errorsFirst() {
        QTemporaryDir tmp;
        Profile profile("Test", tmp.path());

        // Plugins: a present master, plus an enabled child whose master is
        // absent (a real problem), plus a DISABLED child whose master is also
        // absent (won't load -> must not be flagged).
        PluginEntry base; base.filename = "Base.esm"; base.isMaster = true;
        profile.pluginList().append(base);
        PluginEntry child; child.filename = "Child.esp"; child.enabled = true;
        child.masters = {"Absent.esm"};
        profile.pluginList().append(child);
        PluginEntry off; off.filename = "Disabled.esp"; off.enabled = false;
        off.masters = {"AlsoAbsent.esm"};
        profile.pluginList().append(off);

        // Mods: one flagged needs-rerun (no longer a health issue), one ordinary
        // dependency target.
        ModEntry f; f.type = EntryType::Mod; f.id = "modF"; f.name = "Fomod Mod";
        f.enabled = true; f.fomodStatus = "needs-rerun";
        profile.modList().append(f);
        ModEntry d; d.type = EntryType::Mod; d.id = "modD"; d.name = "Dep Mod";
        d.enabled = true; profile.modList().append(d);

        // A conflict between two mods.
        ConflictIndex conflicts;
        conflicts.recordConflict("Data/foo.dll", "modF", "modD");

        HealthInputs in;
        in.dependencyWarnings = { {"modD", {"Requires Address Library"}} };
        in.lastDeployWarning  = "localAppData detection failed";
        in.deployed   = false; // not deployed -> an Info issue
        in.deployDirty = false;

        const auto issues = collect(profile, conflicts, in);

        auto countCat = [&](HealthCategory c) {
            int n = 0; for (const auto& i : issues) if (i.category == c) ++n; return n;
        };
        // Only the enabled child is flagged; the disabled one is silent.
        QCOMPARE(countCat(HealthCategory::MissingMaster),     1);
        QCOMPARE(countCat(HealthCategory::MissingDependency), 1);
        QCOMPARE(countCat(HealthCategory::DeployWarning),     1);
        QCOMPARE(countCat(HealthCategory::DeployState),       1);
        QCOMPARE(countCat(HealthCategory::Conflict),          1);

        // A mod whose FOMOD choices can't be reconstructed is not a health issue;
        // it must not surface in the Problems panel (only the disabled-plugin and
        // FOMOD-needs-rerun states are intentionally absent).
        for (const auto& i : issues)
            QVERIFY(i.targetPlugin != "Disabled.esp");

        // Sorted worst-first: the first issue must be the Error (missing master).
        QVERIFY(!issues.isEmpty());
        QCOMPARE(issues.first().severity, HealthSeverity::Error);
        QCOMPARE(issues.first().category, HealthCategory::MissingMaster);

        // The missing-master issue carries the plugin as its jump-to target.
        bool foundPluginTarget = false;
        for (const auto& i : issues)
            if (i.category == HealthCategory::MissingMaster && i.targetPlugin == "Child.esp")
                foundPluginTarget = true;
        QVERIFY(foundPluginTarget);
    }

    void collect_cleanProfileYieldsZeroIssues() {
        QTemporaryDir tmp;
        Profile profile("Clean", tmp.path());
        // One self-consistent plugin (its master is present), no mods, no warnings.
        PluginEntry esm; esm.filename = "Skyrim.esm"; esm.isMaster = true;
        profile.pluginList().append(esm);

        ConflictIndex conflicts; // empty
        HealthInputs in;
        in.deployed   = true;  // deployed + not dirty -> no DeployState issue
        in.deployDirty = false;

        QVERIFY(collect(profile, conflicts, in).isEmpty());
    }
};
QTEST_MAIN(TestHealthCheck)
#include "test_HealthCheck.moc"
