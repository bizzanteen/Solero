#include <QtTest>
#include "loot/LootUserlist.h"

using namespace solero;
using namespace solero::LootUserlist;

// The profile's loot-userlist.yaml, read and edited without a YAML library.
// Rules here decide load order, and both of the traps below fail silently in
// LOOT today: a duplicated rule bloats the file, and a group name that doesn't
// exactly match a defined group aborts the entire sort with nothing but a
// qWarning to show for it.
class TestLootUserlist : public QObject {
    Q_OBJECT

    static QString canonical() {
        return QStringLiteral(
            "plugins:\n"
            "  - name: \"SonS_HDT_patch.esp\"\n"
            "    after:\n"
            "      - \"NW_Sons_of_Skyrim.esp\"\n"
            "    group: \"Late Loaders\"\n"
            "  - name: \"Other.esp\"\n"
            "    req:\n"
            "      - \"Base.esp\"\n"
            "    inc:\n"
            "      - \"Rival.esp\"\n"
            "groups:\n"
            "  - name: \"Late Loaders\"\n"
            "    after:\n"
            "      - \"default\"\n");
    }

    // A real profile's userlist: the whole root mapping sits at indent 2, and
    // scalars are single-quoted.
    static QString indented() {
        return QStringLiteral(
            "  plugins:\n"
            "    - name: 'Sentinel.esp'\n"
            "      group: 'Late Loaders'\n"
            "    - name: 'KiNRefined.esp'\n"
            "      after:\n"
            "        - 'RSChildren.esp'\n");
    }

private slots:
    // ---- rule type names (the wire vocabulary the MCP speaks) ----

    void mapsRuleTypesToTheirYamlKeys() {
        QCOMPARE(ruleTypeName(LootRuleType::After), QStringLiteral("after"));
        QCOMPARE(ruleTypeName(LootRuleType::Req),   QStringLiteral("req"));
        QCOMPARE(ruleTypeName(LootRuleType::Inc),   QStringLiteral("inc"));
        QCOMPARE(ruleTypeName(LootRuleType::Group), QStringLiteral("group"));
    }

    void parsesRuleTypeNamesAndRejectsUnknownOnes() {
        LootRuleType kind = LootRuleType::Inc;
        QVERIFY(ruleTypeFromName("after", &kind));
        QCOMPARE(kind, LootRuleType::After);
        QVERIFY(ruleTypeFromName("group", &kind));
        QCOMPARE(kind, LootRuleType::Group);
        QVERIFY(!ruleTypeFromName("before", &kind));
        QVERIFY(!ruleTypeFromName("", &kind));
    }

    // ---- parsing ----

    void parsesEveryRuleKindOffOnePlugin() {
        const auto plugins = parsePlugins(canonical());
        QCOMPARE(plugins.size(), 2);
        QCOMPARE(plugins[0].name,  QStringLiteral("SonS_HDT_patch.esp"));
        QCOMPARE(plugins[0].after, QStringList{"NW_Sons_of_Skyrim.esp"});
        QCOMPARE(plugins[0].group, QStringLiteral("Late Loaders"));
        QVERIFY(plugins[0].req.isEmpty());
        QCOMPARE(plugins[1].name, QStringLiteral("Other.esp"));
        QCOMPARE(plugins[1].req,  QStringList{"Base.esp"});
        QCOMPARE(plugins[1].inc,  QStringList{"Rival.esp"});
        QVERIFY(plugins[1].group.isEmpty());
    }

    // LOOT accepts a list item as either a bare scalar or a mapping with a
    // `name:` key, and the editor's own snippet buttons emit the mapping form -
    // so a userlist can legitimately contain both.
    void parsesListItemsWrittenAsNameMappings() {
        const auto plugins = parsePlugins(QStringLiteral(
            "plugins:\n"
            "  - name: \"A.esp\"\n"
            "    after:\n"
            "      - name: \"B.esp\"\n"
            "      - \"C.esp\"\n"));
        QCOMPARE(plugins.size(), 1);
        QCOMPARE(plugins[0].after, (QStringList{"B.esp", "C.esp"}));
    }

    void parsesNothingOutOfAnEmptyOrGrouplessFile() {
        QVERIFY(parsePlugins("").isEmpty());
        QVERIFY(parseGroups("").isEmpty());
        QVERIFY(parseGroups(QStringLiteral("plugins:\n  - name: \"A.esp\"\n")).isEmpty());
    }

    void parsesTheDefinedGroupNames() {
        QCOMPARE(parseGroups(canonical()), QStringList{"Late Loaders"});
    }

    // A document whose single root mapping is uniformly indented is still valid
    // YAML, and LOOT reads it happily - the userlist in the author's own profile
    // looks exactly like this. Anchoring on column 0 would report it as empty.
    void parsesAFileWhoseTopLevelBlockIsIndented() {
        const auto plugins = parsePlugins(indented());
        QCOMPARE(plugins.size(), 2);
        QCOMPARE(plugins[0].name,  QStringLiteral("Sentinel.esp"));
        QCOMPARE(plugins[0].group, QStringLiteral("Late Loaders"));
        QCOMPARE(plugins[1].name,  QStringLiteral("KiNRefined.esp"));
        QCOMPARE(plugins[1].after, QStringList{"RSChildren.esp"});
    }

    // ---- adding ----

    void addsARuleToAFileThatHasNone() {
        const QString out = addRule("", "A.esp", LootRuleType::After, "B.esp");
        const auto plugins = parsePlugins(out);
        QCOMPARE(plugins.size(), 1);
        QCOMPARE(plugins[0].name,  QStringLiteral("A.esp"));
        QCOMPARE(plugins[0].after, QStringList{"B.esp"});
    }

    // The reason this matters: the MCP will re-issue set_loot_rule whenever a
    // caller can't tell whether the rule is already there. A second identical
    // entry is not harmless - LOOT reads it, and the file grows every retry.
    void addingTheSameRuleTwiceChangesNothing() {
        const QString once  = addRule("", "A.esp", LootRuleType::After, "B.esp");
        const QString twice = addRule(once, "A.esp", LootRuleType::After, "B.esp");
        QCOMPARE(twice, once);
    }

    void addingTheSameGroupTwiceChangesNothing() {
        const QString once  = addRule("", "A.esp", LootRuleType::Group, "Late Loaders");
        const QString twice = addRule(once, "A.esp", LootRuleType::Group, "Late Loaders");
        QCOMPARE(twice, once);
    }

    // A plugin belongs to exactly one group, so a second group assignment
    // replaces the first rather than stacking up.
    void reassigningAGroupReplacesTheOldOne() {
        QString y = addRule("", "A.esp", LootRuleType::Group, "Early");
        y = addRule(y, "A.esp", LootRuleType::Group, "Late");
        const auto plugins = parsePlugins(y);
        QCOMPARE(plugins.size(), 1);
        QCOMPARE(plugins[0].group, QStringLiteral("Late"));
    }

    void addsASecondTargetUnderTheSameKey() {
        QString y = addRule("", "A.esp", LootRuleType::After, "B.esp");
        y = addRule(y, "A.esp", LootRuleType::After, "C.esp");
        QCOMPARE(parsePlugins(y).size(), 1);
        QCOMPARE(parsePlugins(y)[0].after, (QStringList{"B.esp", "C.esp"}));
    }

    // Missing the indented block would mean appending a second `plugins:` at
    // column 0 - a duplicate top-level key, which LOOT rejects outright. The
    // rule would look saved and the whole userlist would stop working.
    void addsIntoAnIndentedBlockRatherThanStartingASecondOne() {
        const QString out = addRule(indented(), "New.esp", LootRuleType::After, "Old.esp");
        int headers = 0;
        for (const QString& line : out.split('\n'))
            if (line.trimmed() == QStringLiteral("plugins:")) ++headers;
        QCOMPARE(headers, 1);

        const auto plugins = parsePlugins(out);
        QCOMPARE(plugins.size(), 3);
        QCOMPARE(plugins[2].name,  QStringLiteral("New.esp"));
        QCOMPARE(plugins[2].after, QStringList{"Old.esp"});
        // and the entries that were already there are untouched
        QCOMPARE(plugins[0].group, QStringLiteral("Late Loaders"));
        QCOMPARE(plugins[1].after, QStringList{"RSChildren.esp"});
    }

    // Splicing fixed 2/4/6-space indents into a block that starts deeper would
    // put the new lines at the wrong nesting level.
    void matchesTheIndentationTheBlockAlreadyUses() {
        const QString out = addRule(indented(), "KiNRefined.esp", LootRuleType::After,
                                    "RSChildren - Patch.esp");
        QCOMPARE(parsePlugins(out).size(), 2);
        QCOMPARE(parsePlugins(out)[1].after,
                 (QStringList{"RSChildren.esp", "RSChildren - Patch.esp"}));

        const QString withGroup = addRule(out, "KiNRefined.esp", LootRuleType::Group, "Late Loaders");
        QCOMPARE(parsePlugins(withGroup)[1].group, QStringLiteral("Late Loaders"));
    }

    // ---- removing ----

    void removesOneTargetAndLeavesTheRest() {
        QString y = addRule("", "A.esp", LootRuleType::After, "B.esp");
        y = addRule(y, "A.esp", LootRuleType::After, "C.esp");
        bool removed = false;
        y = removeRule(y, "A.esp", LootRuleType::After, "B.esp", &removed);
        QVERIFY(removed);
        QCOMPARE(parsePlugins(y)[0].after, QStringList{"C.esp"});
    }

    // An `after:` key with no items left is not "an empty rule", it is a null
    // value that LOOT complains about - so the key has to go with its last item.
    void removingTheLastTargetDropsTheKeyAndTheEntry() {
        QString y = addRule("", "A.esp", LootRuleType::After, "B.esp");
        y = removeRule(y, "A.esp", LootRuleType::After, "B.esp");
        QVERIFY(parsePlugins(y).isEmpty());
        QVERIFY(!y.contains("after"));
        QVERIFY(!y.contains("A.esp"));
    }

    void removingAGroupLeavesTheOtherRulesAlone() {
        QString y = addRule("", "A.esp", LootRuleType::After, "B.esp");
        y = addRule(y, "A.esp", LootRuleType::Group, "Late");
        bool removed = false;
        y = removeRule(y, "A.esp", LootRuleType::Group, "Late", &removed);
        QVERIFY(removed);
        const auto plugins = parsePlugins(y);
        QCOMPARE(plugins.size(), 1);
        QVERIFY(plugins[0].group.isEmpty());
        QCOMPARE(plugins[0].after, QStringList{"B.esp"});
    }

    void removingARuleThatIsntThereReportsItAndChangesNothing() {
        const QString y = addRule("", "A.esp", LootRuleType::After, "B.esp");
        bool removed = true;
        const QString out = removeRule(y, "A.esp", LootRuleType::After, "Z.esp", &removed);
        QVERIFY(!removed);
        QCOMPARE(out, y);

        removed = true;
        QCOMPARE(removeRule(y, "Nope.esp", LootRuleType::After, "B.esp", &removed), y);
        QVERIFY(!removed);
    }

    void survivesASetListSortDeleteRoundTrip() {
        QString y = addRule("", "SonS_HDT_patch.esp", LootRuleType::After,
                            "NW_Sons_of_Skyrim.esp");
        y = addRule(y, "SonS_HDT_patch.esp", LootRuleType::After,
                    "NW_Sons_of_Skyrim - My fixes by Xtudo.esp");
        QCOMPARE(parsePlugins(y)[0].after.size(), 2);

        y = removeRule(y, "SonS_HDT_patch.esp", LootRuleType::After,
                       "NW_Sons_of_Skyrim.esp");
        QCOMPARE(parsePlugins(y)[0].after,
                 QStringList{"NW_Sons_of_Skyrim - My fixes by Xtudo.esp"});

        y = removeRule(y, "SonS_HDT_patch.esp", LootRuleType::After,
                       "NW_Sons_of_Skyrim - My fixes by Xtudo.esp");
        QVERIFY(parsePlugins(y).isEmpty());
    }

    // ---- group validation ----

    void acceptsAGroupNameThatMatchesExactly() {
        QCOMPARE(validateGroupName("Late Loaders", {"default", "Late Loaders"}),
                 QString());
    }

    // The trap: LOOT group names are case-sensitive, and a name that only
    // differs in case is not "close enough" - it is undefined, and an undefined
    // group aborts the whole sort. Say so, and say what the real name is.
    void rejectsACaseMismatchAndNamesTheRealGroup() {
        const QString msg = validateGroupName("late loaders", {"default", "Late Loaders"});
        QVERIFY(!msg.isEmpty());
        QVERIFY(msg.contains("case-sensitive"));
        QVERIFY(msg.contains("Late Loaders"));
    }

    void rejectsAnUndefinedGroupAndListsTheValidOnes() {
        const QString msg = validateGroupName("Nonsense", {"default", "Late Loaders"});
        QVERIFY(!msg.isEmpty());
        QVERIFY(msg.contains("default"));
        QVERIFY(msg.contains("Late Loaders"));
    }

    void rejectsAnEmptyGroupName() {
        QVERIFY(!validateGroupName("", {"default"}).isEmpty());
    }
};

QTEST_MAIN(TestLootUserlist)
#include "test_LootUserlist.moc"
