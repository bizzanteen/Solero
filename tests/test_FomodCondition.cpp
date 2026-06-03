#include <QtTest>
#include <QDomDocument>
#include "fomod/FomodCondition.h"
using namespace solero;

static QDomElement parseXml(const QString& xml) {
    static QDomDocument doc;
    doc.setContent(xml);
    return doc.documentElement();
}

class TestFomodCondition : public QObject {
    Q_OBJECT
private slots:
    void emptyCondition_isTrue() {
        FomodCondition c;
        QVERIFY(c.evaluate({}, [](const QString&){ return false; }));
    }
    void flagDependency_matches() {
        auto el = parseXml(R"(<dependencies operator="And">
            <flagDependency flag="A" value="on"/></dependencies>)");
        FomodCondition c = FomodCondition::parse(el);
        QHash<QString,QString> flags{{"A","on"}};
        QVERIFY(c.evaluate(flags, [](const QString&){ return false; }));
        QHash<QString,QString> wrong{{"A","off"}};
        QVERIFY(!c.evaluate(wrong, [](const QString&){ return false; }));
    }
    void andRequiresAll() {
        auto el = parseXml(R"(<dependencies operator="And">
            <flagDependency flag="A" value="1"/>
            <flagDependency flag="B" value="1"/></dependencies>)");
        FomodCondition c = FomodCondition::parse(el);
        QVERIFY(c.evaluate({{"A","1"},{"B","1"}}, [](const QString&){ return false; }));
        QVERIFY(!c.evaluate({{"A","1"}}, [](const QString&){ return false; }));
    }
    void orRequiresAny() {
        auto el = parseXml(R"(<dependencies operator="Or">
            <flagDependency flag="A" value="1"/>
            <flagDependency flag="B" value="1"/></dependencies>)");
        FomodCondition c = FomodCondition::parse(el);
        QVERIFY(c.evaluate({{"B","1"}}, [](const QString&){ return false; }));
        QVERIFY(!c.evaluate({{"C","1"}}, [](const QString&){ return false; }));
    }
    void fileDependency_active() {
        auto el = parseXml(R"(<dependencies operator="And">
            <fileDependency file="Skyrim.esm" state="Active"/></dependencies>)");
        FomodCondition c = FomodCondition::parse(el);
        auto present = [](const QString& f){ return f.compare("Skyrim.esm", Qt::CaseInsensitive)==0; };
        QVERIFY(c.evaluate({}, present));
        QVERIFY(!c.evaluate({}, [](const QString&){ return false; }));
    }
    void nestedDependencies() {
        auto el = parseXml(R"(<dependencies operator="Or">
            <flagDependency flag="A" value="1"/>
            <dependencies operator="And">
              <flagDependency flag="B" value="1"/>
              <flagDependency flag="C" value="1"/>
            </dependencies></dependencies>)");
        FomodCondition c = FomodCondition::parse(el);
        QVERIFY(c.evaluate({{"B","1"},{"C","1"}}, [](const QString&){ return false; }));
        QVERIFY(!c.evaluate({{"B","1"}}, [](const QString&){ return false; }));
    }
};
QTEST_MAIN(TestFomodCondition)
#include "test_FomodCondition.moc"
