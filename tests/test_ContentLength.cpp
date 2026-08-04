#include <QtTest>
#include "tools/ContentLength.h"
using namespace solero;

// Parsing Content-Length out of a `curl -sIL` header dump, used to turn a big
// download into a determinate progress bar.
class TestContentLength : public QObject {
    Q_OBJECT
private slots:
    void readsASimpleHeader() {
        QCOMPARE(contentLengthFromHeaders(
            "HTTP/1.1 200 OK\r\nContent-Length: 230123456\r\n\r\n"), 230123456LL);
    }

    // Header names are case-insensitive, and servers differ on the casing.
    void isCaseInsensitive() {
        QCOMPARE(contentLengthFromHeaders("content-length: 42\r\n"), 42LL);
        QCOMPARE(contentLengthFromHeaders("CONTENT-LENGTH:42\r\n"), 42LL);
    }

    // curl -L prints one header block per hop. The size that matters belongs to
    // the FINAL response, not the 302 that redirected us there.
    void takesTheFinalResponseAcrossRedirects() {
        QCOMPARE(contentLengthFromHeaders(
            "HTTP/1.1 302 Found\r\nContent-Length: 0\r\nLocation: https://cdn/x\r\n\r\n"
            "HTTP/1.1 200 OK\r\nContent-Length: 230123456\r\n\r\n"), 230123456LL);
    }

    // Must not match a header that merely ends in "content-length".
    void ignoresLookalikeHeaders() {
        QCOMPARE(contentLengthFromHeaders(
            "X-Original-Content-Length: 99\r\nContent-Length: 7\r\n"), 7LL);
    }

    // No usable size -> -1, so the caller keeps an indeterminate bar rather than
    // showing a bogus percentage.
    void reportsUnknownWhenAbsentOrUnparsable() {
        QCOMPARE(contentLengthFromHeaders("HTTP/1.1 200 OK\r\n\r\n"), -1LL);
        QCOMPARE(contentLengthFromHeaders(""), -1LL);
        QCOMPARE(contentLengthFromHeaders("Content-Length: banana\r\n"), -1LL);
        QCOMPARE(contentLengthFromHeaders("Transfer-Encoding: chunked\r\n"), -1LL);
    }
};

QTEST_MAIN(TestContentLength)
#include "test_ContentLength.moc"
