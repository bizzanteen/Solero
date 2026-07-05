#include <QtTest>
#include <QDir>
#include <QFileInfo>
#include "report/Redactor.h"

using namespace solero;

class TestRedactor : public QObject { Q_OBJECT
private slots:
    // The canonical QDir::homePath() form is replaced with "~".
    void homePath_replaced() {
        const QString home = QDir::homePath();
        const QString in = "opening " + home + "/.local/share/solero/logs/solero.log now";
        const QString out = redact(in);
        QVERIFY(!out.contains(home));
        QCOMPARE(out, QString("opening ~/.local/share/solero/logs/solero.log now"));
    }

    // Both the /home/<user> and /var/home/<user> spellings collapse to "~", and the
    // longer /var/home form doesn't leave a stray "/var~".
    void homeAndVarHome_replaced() {
        const QString user = QFileInfo(QDir::homePath()).fileName();
        QVERIFY(!user.isEmpty());

        const QString a = redact("path=/home/" + user + "/mods/Foo.esp");
        QCOMPARE(a, QString("path=~/mods/Foo.esp"));

        const QString b = redact("path=/var/home/" + user + "/mods/Foo.esp");
        QCOMPARE(b, QString("path=~/mods/Foo.esp"));
        QVERIFY(!b.contains("/var~"));
    }

    // Unrelated text (and other users' paths) is left intact.
    void otherText_untouched() {
        const QString in = "no personal paths here: /usr/lib, /home/someoneelse/x, 42%";
        QCOMPARE(redact(in), in);
    }

    // Multiple occurrences in one string are all redacted.
    void multipleOccurrences() {
        const QString home = QDir::homePath();
        const QString in = home + "/a and " + home + "/b";
        QCOMPARE(redact(in), QString("~/a and ~/b"));
    }
};

QTEST_MAIN(TestRedactor)
#include "test_Redactor.moc"
