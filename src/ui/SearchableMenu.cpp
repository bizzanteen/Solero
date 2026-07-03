#include "SearchableMenu.h"
#include "MenuFilter.h"

#include <QLineEdit>
#include <QWidgetAction>

namespace solero {

SearchableMenu::SearchableMenu(const QString& title, QWidget* parent)
    : QMenu(title, parent) {
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(QStringLiteral("Search categories") + QChar(0x2026));
    m_edit->setClearButtonEnabled(true);

    auto* wa = new QWidgetAction(this);
    wa->setDefaultWidget(m_edit);
    addAction(wa);

    connect(m_edit, &QLineEdit::textChanged, this,
            [this](const QString& t){ applyFilter(t); });

    // Enter activates the first visible/enabled item, then closes the menu.
    connect(m_edit, &QLineEdit::returnPressed, this, [this]{
        for (QAction* a : std::as_const(m_items)) {
            if (a->isVisible() && a->isEnabled()) {
                a->trigger();
                close();
                return;
            }
        }
    });

    // Reset the filter and focus the search box each time the menu opens.
    connect(this, &QMenu::aboutToShow, this, [this]{
        m_edit->clear();
        m_edit->setFocus();
    });
}

QAction* SearchableMenu::addItem(const QString& text, std::function<void()> onTriggered) {
    QAction* a = addAction(text, std::move(onTriggered));
    m_items.append(a);
    return a;
}

void SearchableMenu::applyFilter(const QString& text) {
    for (QAction* a : std::as_const(m_items))
        a->setVisible(menuFilterMatch(text, a->text()));
}

} // namespace solero
