#include <QtTest>
#include "core/StagingFolder.h"
#include "core/Types.h"

using namespace solero;

class TestStagingFolder : public QObject {
    Q_OBJECT
private slots:
    void sanitize_illegalChars() {
        QCOMPARE(sanitizeStagingFolder("a/b\\c:d*e?f\"g<h>i|j"),
                 QString("a_b_c_d_e_f_g_h_i_j"));
    }
    void sanitize_controlChars() {
        QString in = QString("a") + QChar(0x07) + "b" + QChar(0x1F) + "c";
        QCOMPARE(sanitizeStagingFolder(in), QString("a_b_c"));
    }
    void sanitize_collapseWhitespace() {
        QCOMPARE(sanitizeStagingFolder("foo   bar\t\tbaz"), QString("foo bar baz"));
    }
    void sanitize_trimLeadingTrailingWhitespace() {
        QCOMPARE(sanitizeStagingFolder("  hello world  "), QString("hello world"));
    }
    void sanitize_trailingDot() {
        QCOMPARE(sanitizeStagingFolder("My Mod..."), QString("My Mod"));
        QCOMPARE(sanitizeStagingFolder("a.b.c."), QString("a.b.c"));
    }
    void sanitize_trailingDotAndSpace() {
        // trailing whitespace and dots both stripped
        QCOMPARE(sanitizeStagingFolder("name . . "), QString("name"));
    }
    void sanitize_lengthCapAscii() {
        QString in(200, QChar('x'));
        QString out = sanitizeStagingFolder(in);
        QCOMPARE(out.toUtf8().size(), 150);
    }
    void sanitize_lengthCapCharBoundary() {
        // Each 'é' is 2 UTF-8 bytes; 100 of them = 200 bytes. Cap to 150 bytes
        // must land on a char boundary (75 chars = 150 bytes), not mid-codepoint.
        QString in(100, QChar(0x00E9)); // é
        QString out = sanitizeStagingFolder(in);
        QVERIFY(out.toUtf8().size() <= 150);
        // Round-trips cleanly => no truncated multibyte sequence.
        QCOMPARE(QString::fromUtf8(out.toUtf8()), out);
        QCOMPARE(out.size(), 75);
    }
    void sanitize_reservedNames() {
        QCOMPARE(sanitizeStagingFolder("CON"), QString("_CON"));
        QCOMPARE(sanitizeStagingFolder("con"), QString("_con"));
        QCOMPARE(sanitizeStagingFolder("NUL"), QString("_NUL"));
        QCOMPARE(sanitizeStagingFolder("COM1"), QString("_COM1"));
        QCOMPARE(sanitizeStagingFolder("LPT9"), QString("_LPT9"));
        QCOMPARE(sanitizeStagingFolder("Prn"), QString("_Prn"));
        // not reserved
        QCOMPARE(sanitizeStagingFolder("COM0"), QString("COM0"));
        QCOMPARE(sanitizeStagingFolder("COM10"), QString("COM10"));
        QCOMPARE(sanitizeStagingFolder("CONSOLE"), QString("CONSOLE"));
    }
    void sanitize_empty() {
        QCOMPARE(sanitizeStagingFolder(""), QString("_"));
        QCOMPARE(sanitizeStagingFolder("   "), QString("_"));
        QCOMPARE(sanitizeStagingFolder("..."), QString("_"));
    }
    void unique_noCollision() {
        QSet<QString> taken;
        QCOMPARE(uniqueStagingFolder("My Mod", taken), QString("My Mod"));
    }
    void unique_collisionSuffix() {
        QSet<QString> taken{ "my mod" };
        QCOMPARE(uniqueStagingFolder("My Mod", taken), QString("My Mod (2)"));
    }
    void unique_multipleCollisions() {
        QSet<QString> taken{ "my mod", "my mod (2)", "my mod (3)" };
        QCOMPARE(uniqueStagingFolder("My Mod", taken), QString("My Mod (4)"));
    }
    void unique_caseInsensitive() {
        QSet<QString> taken{ "my mod" };
        // base differs only by case -> still a collision
        QCOMPARE(uniqueStagingFolder("MY MOD", taken), QString("MY MOD (2)"));
    }
    void unique_suffixLengthTrim() {
        QString base(150, QChar('x'));
        QSet<QString> taken{ base.toLower() };
        QString out = uniqueStagingFolder(base, taken);
        QVERIFY(out.toUtf8().size() <= 150);
        QVERIFY(out.endsWith(" (2)"));
    }
    // Output mods created on the frictionless wizard path (MainWindow::
    // ensureOutputMod) must get a FRIENDLY staging folder derived from the
    // preset's output-mod name - never a bare UUID like "mods/<uuid>/Data".
    void outputMod_friendlyFolderNotUuid() {
        const QString name = "Radium Output";
        QSet<QString> taken;
        const QString folder =
            uniqueStagingFolder(sanitizeStagingFolder(name), taken);
        QCOMPARE(folder, QString("Radium Output"));
        // A UUID contains hyphens; the friendly folder must not look like one.
        QVERIFY(!folder.contains('-'));
        // On collision it stays friendly (suffixed), still never a UUID.
        QSet<QString> taken2{ "radium output" };
        const QString folder2 =
            uniqueStagingFolder(sanitizeStagingFolder(name), taken2);
        QCOMPARE(folder2, QString("Radium Output (2)"));
        QVERIFY(!folder2.contains('-'));
    }
    // new output mods are profile-qualified on disk ("<Profile> - <name>") so two
    // profiles producing the same plain output name (e.g. "Radium Output") don't
    // collide on a single shared mods/ folder. Display name stays plain elsewhere.
    void outputMod_profileQualifiedFolder() {
        const QString base = "CSVO - Radium Output"; // profile->name() + " - " + name
        QSet<QString> taken;
        const QString folder = uniqueStagingFolder(sanitizeStagingFolder(base), taken);
        QCOMPARE(folder, QString("CSVO - Radium Output"));
        // A different profile producing the same plain name lands on a distinct folder.
        const QString base2 = "Test - Radium Output";
        const QString folder2 = uniqueStagingFolder(sanitizeStagingFolder(base2), taken);
        QCOMPARE(folder2, QString("Test - Radium Output"));
        QVERIFY(folder != folder2);
    }
};

QTEST_MAIN(TestStagingFolder)
#include "test_StagingFolder.moc"
