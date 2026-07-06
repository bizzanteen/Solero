// Theme options: the pure palette-building logic (mode + accent), independent of
// AppConfig and the filesystem.
#include <QtTest>
#include <QApplication>
#include <QPalette>
#include <QColor>
#include "ui/ThemeAdapter.h"
using namespace solero;

static int lightnessOf(const QColor& c) { return c.lightness(); }

class TestThemeAdapter : public QObject {
    Q_OBJECT
private slots:
    void darkMode_hasDarkWindow() {
        QPalette base; // system fallback, unused for explicit modes
        QPalette p = ThemeAdapter::buildPalette("dark", QColor(), base);
        QVERIFY(lightnessOf(p.color(QPalette::Window)) < 128);
        QVERIFY(lightnessOf(p.color(QPalette::WindowText)) > 128); // legible text on dark
    }
    void lightMode_hasLightWindow() {
        QPalette base;
        QPalette p = ThemeAdapter::buildPalette("light", QColor(), base);
        QVERIFY(lightnessOf(p.color(QPalette::Window)) > 128);
        QVERIFY(lightnessOf(p.color(QPalette::WindowText)) < 128); // dark text on light
    }
    void accent_overridesHighlight() {
        QPalette base;
        const QColor accent("#3daee9");
        QPalette p = ThemeAdapter::buildPalette("dark", accent, base);
        QCOMPARE(p.color(QPalette::Highlight), accent);
        // Highlighted text must contrast with the accent (not equal to it).
        QVERIFY(p.color(QPalette::HighlightedText) != accent);
    }
    void systemMode_noAccent_returnsBaseUnchanged() {
        QPalette base;
        base.setColor(QPalette::Window, QColor("#123456"));
        QPalette p = ThemeAdapter::buildPalette("system", QColor(), base);
        QCOMPARE(p.color(QPalette::Window), QColor("#123456"));
    }
    void invalidAccent_isIgnored() {
        QPalette base;
        QPalette p = ThemeAdapter::buildPalette("dark", QColor(), base); // invalid accent
        const QColor hl = p.color(QPalette::Highlight);
        QVERIFY(hl.isValid()); // dark palette still supplies a highlight
    }

    // GNOME "System" following
    // gsettings prints values single-quoted (e.g. 'prefer-dark').
    void gnomeScheme_darkVsLight() {
        QVERIFY(ThemeAdapter::gnomeSchemeIsDark("'prefer-dark'"));
        QVERIFY(ThemeAdapter::gnomeSchemeIsDark("prefer-dark\n"));
        QVERIFY(!ThemeAdapter::gnomeSchemeIsDark("'prefer-light'"));
        QVERIFY(!ThemeAdapter::gnomeSchemeIsDark("'default'"));
        QVERIFY(!ThemeAdapter::gnomeSchemeIsDark("")); // unknown => not dark
    }
    void gnomeAccent_knownNamesMapToColours() {
        QVERIFY(ThemeAdapter::gnomeAccentColor("'blue'").isValid());
        QCOMPARE(ThemeAdapter::gnomeAccentColor("'blue'"), QColor("#3584e4"));
        QVERIFY(ThemeAdapter::gnomeAccentColor("teal").isValid());   // unquoted tolerated
        QVERIFY(ThemeAdapter::gnomeAccentColor("'purple'").isValid());
    }
    void gnomeAccent_unknownIsInvalid() {
        QVERIFY(!ThemeAdapter::gnomeAccentColor("'chartreuse'").isValid());
        QVERIFY(!ThemeAdapter::gnomeAccentColor("").isValid());
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv); // QPalette/QColor need a QGuiApplication
    TestThemeAdapter tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_ThemeAdapter.moc"
