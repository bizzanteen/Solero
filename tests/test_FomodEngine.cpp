#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "fomod/FomodEngine.h"
using namespace solero;

static QString writeConfig(QTemporaryDir& tmp, const QString& xml) {
    QString dir = tmp.path() + "/fomod";
    QDir().mkpath(dir);
    QString path = dir + "/ModuleConfig.xml";
    QFile f(path); f.open(QIODevice::WriteOnly); f.write(xml.toUtf8()); f.close();
    return path;
}

class TestFomodEngine : public QObject {
    Q_OBJECT
private slots:
    void parsesStepsAndGroups() {
        QTemporaryDir tmp;
        QString cfg = writeConfig(tmp, R"(<?xml version="1.0"?>
<config><moduleName>My Mod</moduleName>
 <installSteps><installStep name="Main">
  <optionalFileGroups><group name="Core" type="SelectExactlyOne">
   <plugins><plugin name="Full"><description>All</description>
     <files><file source="full/a.esp" destination="a.esp"/></files>
     <typeDescriptor><type name="Recommended"/></typeDescriptor></plugin>
   <plugin name="Lite"><description>Less</description>
     <files><file source="lite/a.esp" destination="a.esp"/></files>
     <typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
   </plugins></group></optionalFileGroups>
 </installStep></installSteps></config>)");

        FomodEngine engine;
        QVERIFY(engine.load(cfg));
        QCOMPARE(engine.module().moduleName, QString("My Mod"));
        QCOMPARE(engine.module().steps.size(), 1);
        QCOMPARE(engine.module().steps[0].groups.size(), 1);
        QCOMPARE(engine.module().steps[0].groups[0].type, GroupType::ExactlyOne);
        QCOMPARE(engine.module().steps[0].groups[0].options.size(), 2);
        QCOMPARE(engine.module().steps[0].groups[0].options[0].baseType, OptionType::Recommended);
    }
    void requiredFilesAlwaysInstalled() {
        QTemporaryDir tmp;
        QString cfg = writeConfig(tmp, R"(<?xml version="1.0"?>
<config><moduleName>M</moduleName>
 <requiredInstallFiles><file source="core/req.esp" destination="req.esp"/></requiredInstallFiles>
 <installSteps><installStep name="S"><optionalFileGroups>
   <group name="G" type="SelectAny"><plugins>
     <plugin name="Opt"><files><file source="opt/x.esp" destination="x.esp"/></files>
       <typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
   </plugins></group></optionalFileGroups></installStep></installSteps></config>)");
        FomodEngine engine; QVERIFY(engine.load(cfg));
        auto files = engine.collectFiles({});
        QCOMPARE(files.size(), 1);
        QCOMPARE(files[0].source, QString("core/req.esp"));
        auto files2 = engine.collectFiles({{"0/0/0", true}});
        QCOMPARE(files2.size(), 2);
    }
    void conditionalInstallsApplyWhenFlagSet() {
        QTemporaryDir tmp;
        QString cfg = writeConfig(tmp, R"(<?xml version="1.0"?>
<config><moduleName>M</moduleName>
 <installSteps><installStep name="S"><optionalFileGroups>
   <group name="G" type="SelectAny"><plugins>
     <plugin name="Patch"><files/>
       <conditionFlags><flag name="usePatch">on</flag></conditionFlags>
       <typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
   </plugins></group></optionalFileGroups></installStep></installSteps>
 <conditionalFileInstalls><patterns><pattern>
   <dependencies operator="And"><flagDependency flag="usePatch" value="on"/></dependencies>
   <files><file source="patch/p.esp" destination="p.esp"/></files>
 </pattern></patterns></conditionalFileInstalls></config>)");
        FomodEngine engine; QVERIFY(engine.load(cfg));
        QCOMPARE(engine.collectFiles({}).size(), 0);
        auto files = engine.collectFiles({{"0/0/0", true}});
        QCOMPARE(files.size(), 1);
        QCOMPARE(files[0].source, QString("patch/p.esp"));
    }
    void stepVisibilityRespectsFlags() {
        QTemporaryDir tmp;
        QString cfg = writeConfig(tmp, R"(<?xml version="1.0"?>
<config><moduleName>M</moduleName><installSteps>
  <installStep name="One"><optionalFileGroups>
    <group name="G" type="SelectAny"><plugins>
      <plugin name="EnableTwo"><files/>
        <conditionFlags><flag name="two">yes</flag></conditionFlags>
        <typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
    </plugins></group></optionalFileGroups></installStep>
  <installStep name="Two">
    <visible><flagDependency flag="two" value="yes"/></visible>
    <optionalFileGroups><group name="G2" type="SelectAny"><plugins>
      <plugin name="P"><files/><typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
    </plugins></group></optionalFileGroups></installStep>
</installSteps></config>)");
        FomodEngine engine; QVERIFY(engine.load(cfg));
        QVERIFY(!engine.isStepVisible(1, {}));
        QVERIFY(engine.isStepVisible(1, {{"0/0/0", true}}));
    }

    // collectFiles must not include selections from steps that have become
    // hidden after a flag change (stale selections).
    void collectFilesExcludesHiddenSteps() {
        QTemporaryDir tmp;
        QString cfg = writeConfig(tmp, R"(<?xml version="1.0"?>
<config><moduleName>M</moduleName><installSteps>
  <installStep name="One"><optionalFileGroups>
    <group name="G" type="SelectAny"><plugins>
      <plugin name="EnableTwo"><files/>
        <conditionFlags><flag name="two">yes</flag></conditionFlags>
        <typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
    </plugins></group></optionalFileGroups></installStep>
  <installStep name="Two">
    <visible><flagDependency flag="two" value="yes"/></visible>
    <optionalFileGroups><group name="G2" type="SelectAny"><plugins>
      <plugin name="P"><files><file source="two/x.esp" destination="x.esp"/></files>
        <typeDescriptor><type name="Optional"/></typeDescriptor></plugin>
    </plugins></group></optionalFileGroups></installStep>
</installSteps></config>)");
        FomodEngine engine; QVERIFY(engine.load(cfg));
        // Step Two's option selected, but flag "two" not set -> step hidden ->
        // its file must be excluded.
        auto hidden = engine.collectFiles({{"1/0/0", true}});
        QCOMPARE(hidden.size(), 0);
        // With flag set, step Two becomes visible and its file is included.
        auto shown = engine.collectFiles({{"0/0/0", true}, {"1/0/0", true}});
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown[0].source, QString("two/x.esp"));
    }

    // A folder entry with no destination must leave destination empty so it
    // installs into the Data root; a file entry defaults to its source path.
    void folderDestinationDefaultsEmpty() {
        QTemporaryDir tmp;
        QString cfg = writeConfig(tmp, R"(<?xml version="1.0"?>
<config><moduleName>M</moduleName>
 <requiredInstallFiles>
   <folder source="meshes"/>
   <file source="a/b.esp"/>
 </requiredInstallFiles>
 <installSteps/></config>)");
        FomodEngine engine; QVERIFY(engine.load(cfg));
        auto files = engine.collectFiles({});
        QCOMPARE(files.size(), 2);
        QCOMPARE(files[0].isFolder, true);
        QCOMPARE(files[0].destination, QString());        // folder -> empty
        QCOMPARE(files[1].isFolder, false);
        QCOMPARE(files[1].destination, QString("a/b.esp")); // file -> source path
    }
};
QTEST_MAIN(TestFomodEngine)
#include "test_FomodEngine.moc"
