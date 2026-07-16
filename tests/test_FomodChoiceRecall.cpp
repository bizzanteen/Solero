#include <QtTest>
#include <QJsonObject>
#include <QJsonArray>
#include "fomod/FomodChoiceRecall.h"
#include "fomod/FomodEngine.h"
using namespace solero;

// Build a small two-step FOMOD model in memory.
//  Step "Main":
//    Group "Core" (ExactlyOne): options "Full", "Lite"
//    Group "Extras" (Any):      options "Patch A", "Patch B"
//  Step "Textures":
//    Group "Res" (ExactlyOne):  options "2K", "4K"
static FomodModule sampleModel() {
    FomodModule m;
    FomodStep s1; s1.name = "Main";
    FomodGroup g1; g1.name = "Core"; g1.type = GroupType::ExactlyOne;
    g1.options.append(FomodOption{"Full"});
    g1.options.append(FomodOption{"Lite"});
    FomodGroup g2; g2.name = "Extras"; g2.type = GroupType::Any;
    g2.options.append(FomodOption{"Patch A"});
    g2.options.append(FomodOption{"Patch B"});
    s1.groups.append(g1); s1.groups.append(g2);
    FomodStep s2; s2.name = "Textures";
    FomodGroup g3; g3.name = "Res"; g3.type = GroupType::ExactlyOne;
    g3.options.append(FomodOption{"2K"});
    g3.options.append(FomodOption{"4K"});
    s2.groups.append(g3);
    m.steps.append(s1); m.steps.append(s2);
    return m;
}

static QJsonObject step(const QString& name, const QStringList& sel) {
    QJsonArray arr; for (const QString& s : sel) arr.append(s);
    QJsonObject o; o["step"] = name; o["selected"] = arr; return o;
}

class TestFomodChoiceRecall : public QObject {
    Q_OBJECT
private slots:
    void matchesNamesToIndices() {
        FomodModule m = sampleModel();
        QJsonArray steps;
        steps.append(step("Main", {"Lite", "Patch B"}));
        steps.append(step("Textures", {"4K"}));
        QJsonObject saved; saved["steps"] = steps;

        FomodPreset p = buildFomodPreset(m, saved);

        // Lite = step 0, group 0 (Core), opt 1
        const QString kLite = FomodEngine::selKey(0, 0, 1);
        // Patch B = step 0, group 1 (Extras), opt 1
        const QString kPatchB = FomodEngine::selKey(0, 1, 1);
        // 4K = step 1, group 0 (Res), opt 1
        const QString k4K = FomodEngine::selKey(1, 0, 1);

        QVERIFY(p.selection.value(kLite, false));
        QVERIFY(p.selection.value(kPatchB, false));
        QVERIFY(p.selection.value(k4K, false));
        QVERIFY(p.priorKeys.contains(kLite));
        QVERIFY(p.priorKeys.contains(kPatchB));
        QVERIFY(p.priorKeys.contains(k4K));

        // Exactly three picks, nothing else ticked.
        QCOMPARE(p.selection.size(), 3);
        QCOMPARE(p.priorKeys.size(), 3);

        // "Full" / "Patch A" / "2K" were not chosen.
        QVERIFY(!p.selection.value(FomodEngine::selKey(0, 0, 0), false));
        QVERIFY(!p.selection.value(FomodEngine::selKey(0, 1, 0), false));
        QVERIFY(!p.selection.value(FomodEngine::selKey(1, 0, 0), false));
    }

    void skipsUnmatchedNames() {
        FomodModule m = sampleModel();
        QJsonArray steps;
        // Step name not in model + an option name not in the step.
        steps.append(step("Nonexistent Step", {"Full"}));
        steps.append(step("Main", {"Lite", "Ghost Option"}));
        QJsonObject saved; saved["steps"] = steps;

        FomodPreset p = buildFomodPreset(m, saved);

        // Only "Lite" resolves.
        QCOMPARE(p.selection.size(), 1);
        QCOMPARE(p.priorKeys.size(), 1);
        QVERIFY(p.priorKeys.contains(FomodEngine::selKey(0, 0, 1)));
    }

    void toleratesCaseAndWhitespace() {
        FomodModule m = sampleModel();
        QJsonArray steps;
        steps.append(step("  main ", {" lite ", "PATCH a"}));
        QJsonObject saved; saved["steps"] = steps;

        FomodPreset p = buildFomodPreset(m, saved);

        QVERIFY(p.priorKeys.contains(FomodEngine::selKey(0, 0, 1))); // Lite
        QVERIFY(p.priorKeys.contains(FomodEngine::selKey(0, 1, 0))); // Patch A
        QCOMPARE(p.priorKeys.size(), 2);
    }

    void handlesEmptyOrMissing() {
        FomodModule m = sampleModel();
        FomodPreset p = buildFomodPreset(m, QJsonObject{});
        QVERIFY(p.selection.isEmpty());
        QVERIFY(p.priorKeys.isEmpty());
    }

    // Modules often repeat step names (conditionally-visible twins). The saved
    // entries must map onto DIFFERENT module steps in order, not both onto the
    // first one (which used to double-mark its pick-one group).
    void sameNamedTwinStepsMapInOrder() {
        FomodModule m;
        for (int i = 0; i < 2; ++i) {
            FomodStep s; s.name = "Rift Leaf Texture";
            FomodGroup g; g.name = "Color"; g.type = GroupType::ExactlyOne;
            g.options.append(FomodOption{"Yellow"});
            g.options.append(FomodOption{"Orange"});
            s.groups.append(g);
            m.steps.append(s);
        }
        QJsonArray steps;
        steps.append(step("Rift Leaf Texture", {"Yellow"}));
        steps.append(step("Rift Leaf Texture", {"Orange"}));
        QJsonObject saved; saved["steps"] = steps;

        FomodPreset p = buildFomodPreset(m, saved);

        QVERIFY(p.priorKeys.contains(FomodEngine::selKey(0, 0, 0))); // step 0: Yellow
        QVERIFY(p.priorKeys.contains(FomodEngine::selKey(1, 0, 1))); // step 1: Orange
        QCOMPARE(p.priorKeys.size(), 2);
    }

    // A log that accumulated two picks for one pick-one group (older bug) must
    // resolve to a single "previously chosen": the LAST recorded pick.
    void lastPickWinsInExclusiveGroup() {
        FomodModule m = sampleModel();
        QJsonArray steps;
        steps.append(step("Main", {"Full", "Lite"})); // Core is ExactlyOne
        QJsonObject saved; saved["steps"] = steps;

        FomodPreset p = buildFomodPreset(m, saved);

        QCOMPARE(p.priorKeys.size(), 1);
        QVERIFY(p.priorKeys.contains(FomodEngine::selKey(0, 0, 1))); // Lite (last)
        QVERIFY(!p.selection.value(FomodEngine::selKey(0, 0, 0), false));
    }

    // Newer logs carry the exact step index; it wins over first-name matching.
    void stepIndexDisambiguates() {
        FomodModule m;
        for (int i = 0; i < 2; ++i) {
            FomodStep s; s.name = "Options";
            FomodGroup g; g.type = GroupType::ExactlyOne;
            g.options.append(FomodOption{"A"});
            g.options.append(FomodOption{"B"});
            s.groups.append(g);
            m.steps.append(s);
        }
        QJsonObject o = step("Options", {"B"});
        o["stepIndex"] = 1;
        QJsonArray steps; steps.append(o);
        QJsonObject saved; saved["steps"] = steps;

        FomodPreset p = buildFomodPreset(m, saved);

        QCOMPARE(p.priorKeys.size(), 1);
        QVERIFY(p.priorKeys.contains(FomodEngine::selKey(1, 0, 1))); // second step's B
    }

    // The same option name in several groups of one step ("2K" per texture):
    // picks were logged in group order, so they map to successive groups.
    void duplicateOptionNamesAcrossGroups() {
        FomodModule m;
        FomodStep s; s.name = "Textures";
        for (int i = 0; i < 2; ++i) {
            FomodGroup g; g.type = GroupType::ExactlyOne;
            g.options.append(FomodOption{"1K"});
            g.options.append(FomodOption{"2K"});
            s.groups.append(g);
        }
        m.steps.append(s);
        QJsonArray steps;
        steps.append(step("Textures", {"2K", "1K"}));
        QJsonObject saved; saved["steps"] = steps;

        FomodPreset p = buildFomodPreset(m, saved);

        QVERIFY(p.priorKeys.contains(FomodEngine::selKey(0, 0, 1))); // group 0: 2K
        QVERIFY(p.priorKeys.contains(FomodEngine::selKey(0, 1, 0))); // group 1: 1K
        QCOMPARE(p.priorKeys.size(), 2);
    }
};

QTEST_MAIN(TestFomodChoiceRecall)
#include "test_FomodChoiceRecall.moc"
