// Nexus sign-in autofill: which pages get filled, and quoting the values safely.
#include <QtTest>
#include "ui/NexusLoginFill.h"
#include "nexus/NexusCredentials.h"
using namespace solero;

class TestNexusLoginFill : public QObject {
    Q_OBJECT
private slots:
    void matchesTheSignInPage() {
        QVERIFY(isNexusSignInUrl(QUrl("https://users.nexusmods.com/auth/sign_in")));
        QVERIFY(isNexusSignInUrl(QUrl("https://users.nexusmods.com/auth/sign_in/")));
        QVERIFY(isNexusSignInUrl(QUrl(
            "https://users.nexusmods.com/auth/sign_in?redirect_url=https://www.nexusmods.com/skyrimspecialedition")));
        QVERIFY(isNexusSignInUrl(QUrl("https://USERS.NexusMods.com/auth/Sign_In")));
    }

    // A password must never be typed into a page just because its URL looks close.
    void doesNotMatchAnythingElse() {
        QVERIFY(!isNexusSignInUrl(QUrl("https://www.nexusmods.com/auth/sign_in")));
        QVERIFY(!isNexusSignInUrl(QUrl("https://users.nexusmods.com.evil.tld/auth/sign_in")));
        QVERIFY(!isNexusSignInUrl(QUrl("https://users.nexusmods.com/auth/sign_up")));
        QVERIFY(!isNexusSignInUrl(QUrl("https://www.nexusmods.com/skyrimspecialedition/mods/1")));
        QVERIFY(!isNexusSignInUrl(QUrl("https://evil.example/users.nexusmods.com/auth/sign_in")));
        QVERIFY(!isNexusSignInUrl(QUrl()));
    }

    void quotesPlainValues() {
        QCOMPARE(jsQuote("hunter2"), QStringLiteral("'hunter2'"));
        QCOMPARE(jsQuote(""), QStringLiteral("''"));
    }

    // A quote or backslash in a password would otherwise close the JS literal early.
    void escapesQuotesAndBackslashes() {
        QCOMPARE(jsQuote("it's"), QStringLiteral("'it\\'s'"));
        QCOMPARE(jsQuote("a\\b"), QStringLiteral("'a\\\\b'"));
        QCOMPARE(jsQuote("';alert(1);'"), QStringLiteral("'\\';alert(1);\\''"));
        QCOMPARE(jsQuote("line\nbreak"), QStringLiteral("'line\\nbreak'"));
    }

    void escapesTheUnicodeLineSeparators() {
        const QString sep = QString(QChar(0x2028)) + QChar(0x2029);
        QCOMPARE(jsQuote(sep), QStringLiteral("'\\u2028\\u2029'"));
    }

    void fillScriptCarriesBothValuesAndLeavesSubmitAlone() {
        const QString js = buildFillScript("me@example.com", "p'w\\d");
        QVERIFY2(js.contains("'me@example.com'"), qPrintable(js));
        QVERIFY2(js.contains("'p\\'w\\\\d'"), qPrintable(js));
        QVERIFY(js.contains("user_login"));
        QVERIFY(js.contains("getElementById('password')"));
        // Turnstile and 2FA are the user's to clear - never auto-submit.
        QVERIFY(!js.contains("submit()"));
        QVERIFY(!js.contains("click()"));
    }

    // secret-tool prints one attribute per line; take login, not a lookalike.
    void parsesTheLoginOutOfSecretToolSearch() {
        const QString out =
            "[/org/freedesktop/secrets/collection/login/42]\n"
            "label = Solero - Nexus Mods\n"
            "secret = hunter2\n"
            "attribute.service = solero-nexus\n"
            "attribute.login = me@example.com\n";
        QCOMPARE(NexusCredentials::parseLoginAttribute(out),
                 QStringLiteral("me@example.com"));
    }

    void parseReturnsEmptyWhenNothingIsSaved() {
        QCOMPARE(NexusCredentials::parseLoginAttribute(QString()), QString());
        QCOMPARE(NexusCredentials::parseLoginAttribute("no attributes here\n"), QString());
    }
};

QTEST_MAIN(TestNexusLoginFill)
#include "test_NexusLoginFill.moc"
