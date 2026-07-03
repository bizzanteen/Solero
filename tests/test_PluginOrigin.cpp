#include <QtTest>
#include "core/PluginOrigin.h"
using namespace solero;

class TestPluginOrigin : public QObject {
    Q_OBJECT
private slots:
    void buildIndex_ordersProvidersLowToHigh() {
        QStringList ordered{"modA", "modB", "modC"}; // modC highest priority
        QHash<QString, QStringList> byMod;
        byMod.insert("modA", {"Shared.esp"});
        byMod.insert("modB", {"Shared.esp", "OnlyB.esp"});
        byMod.insert("modC", {"Shared.esp"});
        auto idx = PluginOrigin::buildIndex(ordered, byMod);

        QCOMPARE(idx.value("shared.esp"), QStringList({"modA", "modB", "modC"}));
        QCOMPARE(idx.value("onlyb.esp"), QStringList({"modB"}));
        QVERIFY(idx.value("missing.esp").isEmpty());
    }

    void buildIndex_isCaseInsensitiveOnFilename() {
        QStringList ordered{"modA"};
        QHash<QString, QStringList> byMod;
        byMod.insert("modA", {"MixedCase.ESP"});
        auto idx = PluginOrigin::buildIndex(ordered, byMod);
        QCOMPARE(idx.value("mixedcase.esp"), QStringList({"modA"}));
    }

    void winner_isHighestPriority() {
        QCOMPARE(PluginOrigin::winner({"modA", "modB", "modC"}), QString("modC"));
        QCOMPARE(PluginOrigin::winner({}), QString());
        QCOMPARE(PluginOrigin::winner({"only"}), QString("only"));
    }
};
QTEST_MAIN(TestPluginOrigin)
#include "test_PluginOrigin.moc"
