#include <QtTest>
#include "tools/CurlError.h"

using namespace solero;

class TestCurlError : public QObject {
    Q_OBJECT
private slots:
    void emptyStderrReturnsEmpty() {
        QCOMPARE(curlStderrHint(""), QString());
    }
    void humanReadableLineReturned() {
        QCOMPARE(curlStderrHint("curl: (6) Could not resolve host: api.nexusmods.com"),
                 QString("curl: (6) Could not resolve host: api.nexusmods.com"));
    }
    void lastLineOfMultiLineUsed() {
        const QString text = "some preamble\ncurl: (28) Operation timed out after 30000 ms";
        QCOMPARE(curlStderrHint(text),
                 QString("curl: (28) Operation timed out after 30000 ms"));
    }
    void pathPrefixRejected() {
        QCOMPARE(curlStderrHint("/home/user/.cache/something"), QString());
    }
    void exitCodeMarkerRejected() {
        QCOMPARE(curlStderrHint("exit code 22"), QString());
    }
    void errnoMarkerRejected() {
        QCOMPARE(curlStderrHint("errno 111: Connection refused"), QString());
    }
    void blankLinesSkipped() {
        QCOMPARE(curlStderrHint("  \n  \nCould not resolve host: example.com\n  "),
                 QString("Could not resolve host: example.com"));
    }
};

QTEST_GUILESS_MAIN(TestCurlError)
#include "test_CurlError.moc"
