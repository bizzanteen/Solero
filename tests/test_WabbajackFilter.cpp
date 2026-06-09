#include <QtTest>
#include "ui/WabbajackDialog.h"
#include "wabbajack/WabbajackModlist.h"

using namespace solero;
using OF = WabbajackDialog::OfficialFilter;

static WabbajackModlist mk(const QString& title, const QString& author,
                           const QString& desc, bool official,
                           const QStringList& tags) {
    WabbajackModlist m;
    m.title = title;
    m.author = author;
    m.description = desc;
    m.official = official;
    m.tags = tags;
    return m;
}

class TestWabbajackFilter : public QObject { Q_OBJECT
private slots:

    void officialFilter() {
        auto off = mk("Alpha", "A", "", true,  {});
        auto un  = mk("Beta",  "B", "", false, {});
        QVERIFY( WabbajackDialog::passesFilters(off, "", OF::All, {}));
        QVERIFY( WabbajackDialog::passesFilters(un,  "", OF::All, {}));
        QVERIFY( WabbajackDialog::passesFilters(off, "", OF::Official, {}));
        QVERIFY(!WabbajackDialog::passesFilters(un,  "", OF::Official, {}));
        QVERIFY(!WabbajackDialog::passesFilters(off, "", OF::Unofficial, {}));
        QVERIFY( WabbajackDialog::passesFilters(un,  "", OF::Unofficial, {}));
    }

    void tagFilter() {
        auto a = mk("Alpha", "A", "", true, {"Performance", "Graphics"});
        auto b = mk("Beta",  "B", "", true, {"Performance"});
        // Single tag.
        QVERIFY( WabbajackDialog::passesFilters(a, "", OF::All, {"Performance"}));
        QVERIFY( WabbajackDialog::passesFilters(b, "", OF::All, {"Performance"}));
        QVERIFY( WabbajackDialog::passesFilters(a, "", OF::All, {"Graphics"}));
        QVERIFY(!WabbajackDialog::passesFilters(b, "", OF::All, {"Graphics"}));
        // Multiple tags = and.
        QSet<QString> both{"Performance", "Graphics"};
        QVERIFY( WabbajackDialog::passesFilters(a, "", OF::All, both));
        QVERIFY(!WabbajackDialog::passesFilters(b, "", OF::All, both));
        // Empty set = any.
        QVERIFY( WabbajackDialog::passesFilters(b, "", OF::All, {}));
    }

    void searchComposable() {
        auto a = mk("Skyrim Lite", "Ann", "fast list", true, {"Performance"});
        // Search matches title.
        QVERIFY( WabbajackDialog::passesFilters(a, "lite", OF::All, {}));
        // Search matches author.
        QVERIFY( WabbajackDialog::passesFilters(a, "ann", OF::All, {}));
        // Search matches description.
        QVERIFY( WabbajackDialog::passesFilters(a, "fast", OF::All, {}));
        // No match.
        QVERIFY(!WabbajackDialog::passesFilters(a, "zzz", OF::All, {}));
        // Composes with official + tag: all must pass.
        QVERIFY( WabbajackDialog::passesFilters(a, "lite", OF::Official, {"Performance"}));
        QVERIFY(!WabbajackDialog::passesFilters(a, "lite", OF::Unofficial, {"Performance"}));
        QVERIFY(!WabbajackDialog::passesFilters(a, "lite", OF::Official, {"Graphics"}));
    }

    void collectTagsUnion() {
        QList<WabbajackModlist> lists{
            mk("A", "", "", true,  {"Graphics", "Performance"}),
            mk("B", "", "", false, {"performance", "Lore"}),  // diff case kept distinct
            mk("C", "", "", true,  {"  ", ""}),                 // blanks dropped
        };
        QStringList tags = WabbajackDialog::collectTags(lists);
        // Sorted case-insensitively; blanks removed; case-variants both present.
        QVERIFY(tags.contains("Graphics"));
        QVERIFY(tags.contains("Performance"));
        QVERIFY(tags.contains("performance"));
        QVERIFY(tags.contains("Lore"));
        QVERIFY(!tags.contains(""));
        // No empty/whitespace entries.
        for (const QString& t : tags) QVERIFY(!t.trimmed().isEmpty());
        // Sorted ascending (case-insensitive).
        for (int i = 1; i < tags.size(); ++i)
            QVERIFY(QString::compare(tags[i-1], tags[i], Qt::CaseInsensitive) <= 0);
    }
};

QTEST_MAIN(TestWabbajackFilter)
#include "test_WabbajackFilter.moc"
