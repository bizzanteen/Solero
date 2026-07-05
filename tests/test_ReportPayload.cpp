#include <QtTest>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include "report/ReportSubmitter.h"

using namespace solero;

class TestReportPayload : public QObject { Q_OBJECT
private:
    static ReportSubmitter::SystemInfo sampleInfo() {
        ReportSubmitter::SystemInfo si;
        si.appVersion = "0.1.0";
        si.os         = "Fedora Linux 43";
        si.qt         = "6.9.0";
        si.deployMode = "hardlink";
        si.modCount   = 123;
        return si;
    }

private slots:
    // A bug report: [bug] title prefix, sections carry version/os/qt, the user's
    // answers land in the body, and the redacted log is attached verbatim.
    void bugPayload_hasSectionsBodyLog() {
        QMap<QString, QString> fields;
        fields["summary"]      = "Deploy button does nothing";
        fields["whatHappened"] = "I clicked Deploy and nothing happened";
        fields["expected"]     = "Mods should link into the game folder";
        fields["steps"]        = "1. open 2. click Deploy";

        const QString log = "2026-07-05 [solero.app] INFO started\n~ tail line";
        const QJsonObject p = ReportSubmitter::buildPayload(
            ReportSubmitter::Kind::Issue, fields, log, sampleInfo());

        QCOMPARE(p.value("kind").toString(), QString("bug"));
        QVERIFY(p.value("title").toString().startsWith("[bug] "));
        QVERIFY(p.value("title").toString().contains("Deploy button does nothing"));

        const QJsonObject s = p.value("sections").toObject();
        QCOMPARE(s.value("version").toString(), QString("0.1.0"));
        QCOMPARE(s.value("os").toString(),      QString("Fedora Linux 43"));
        QCOMPARE(s.value("qt").toString(),      QString("6.9.0"));
        QCOMPARE(s.value("deployMode").toString(), QString("hardlink"));
        QCOMPARE(s.value("modCount").toInt(),   123);

        const QString body = p.value("body").toString();
        QVERIFY(body.contains("I clicked Deploy and nothing happened"));
        QVERIFY(body.contains("Mods should link into the game folder"));
        QVERIFY(body.contains("1. open 2. click Deploy"));

        QCOMPARE(p.value("log").toString(), log);
    }

    // A crash report: [crash] prefix; with no summary the title falls back to the
    // first log line (the crash marker); the "what were you doing" answer is in body.
    void crashPayload_titleFromFirstLogLine() {
        QMap<QString, QString> fields;
        fields["whatDoing"] = "I was sorting the load order";

        const QString log = "=== CRASH: SIGSEGV (segfault) ===\n#0 0x... foo()";
        const QJsonObject p = ReportSubmitter::buildPayload(
            ReportSubmitter::Kind::Crash, fields, log, sampleInfo());

        QCOMPARE(p.value("kind").toString(), QString("crash"));
        QVERIFY(p.value("title").toString().startsWith("[crash] "));
        QVERIFY(p.value("title").toString().contains("SIGSEGV"));
        QVERIFY(p.value("body").toString().contains("I was sorting the load order"));
    }

    // The payload must never carry a raw home path - the attached log is redacted
    // upstream (gatherLogTail), and buildPayload passes it through unchanged, so a
    // redacted tail stays redacted.
    void payload_containsNoRawHomePath() {
        const QString home = QDir::homePath();
        QMap<QString, QString> fields;
        fields["summary"]      = "x";
        fields["whatHappened"] = "y";
        // gatherLogTail would have already replaced the home path with "~".
        const QString redactedLog = "opened ~/.local/share/solero/logs/solero.log";
        const QJsonObject p = ReportSubmitter::buildPayload(
            ReportSubmitter::Kind::Issue, fields, redactedLog, sampleInfo());

        const QString serialized = QString::fromUtf8(
            QJsonDocument(p).toJson(QJsonDocument::Compact));
        QVERIFY(!serialized.contains(home));
        QVERIFY(serialized.contains("~/.local/share/solero"));
    }

    // The compiled-in relay URL is still the placeholder -> relayConfigured() is false
    // (Send is disabled and the browser fallback offered), unless SOLERO_REPORT_RELAY
    // overrides it.
    void relayConfigured_reflectsPlaceholder() {
        qunsetenv("SOLERO_REPORT_RELAY");
        QCOMPARE(ReportSubmitter::relayUrl(), QString::fromLatin1(kReportRelayUrl));
        QVERIFY(!ReportSubmitter::relayConfigured());

        qputenv("SOLERO_REPORT_RELAY", "https://example.test/report");
        QVERIFY(ReportSubmitter::relayConfigured());
        QCOMPARE(ReportSubmitter::relayUrl(), QString("https://example.test/report"));
        qunsetenv("SOLERO_REPORT_RELAY");
    }
};

QTEST_MAIN(TestReportPayload)
#include "test_ReportPayload.moc"
