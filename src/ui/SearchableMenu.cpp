#include "SearchableMenu.h"
#include "MenuFilter.h"

#include <QLineEdit>
#include <QShowEvent>
#include <QTimer>
#include <QWidgetAction>

namespace solero {

SearchableMenu::SearchableMenu(const QString& title, QWidget* parent)
    : QMenu(title, parent) {
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(QStringLiteral("Search categories") + QChar(0x2026));
    m_edit->setClearButtonEnabled(true);
    m_edit->setFocusPolicy(Qt::StrongFocus);

    auto* wa = new QWidgetAction(this);
    wa->setDefaultWidget(m_edit);
    addAction(wa);

    connect(m_edit, &QLineEdit::textChanged, this,
            [this](const QString& t){ applyFilter(t); });

    // Enter activates the first visible/enabled item, then closes the full
    // menu chain (this submenu and all ancestor QMenus).
    connect(m_edit, &QLineEdit::returnPressed, this, [this]{
        for (QAction* a : std::as_const(m_items)) {
            if (a->isVisible() && a->isEnabled()) {
                a->trigger();
                QMenu* m = this;
                while (m) {
                    m->close();
                    m = qobject_cast<QMenu*>(m->parentWidget());
                }
                return;
            }
        }
    });

    // Reset the filter each time the menu is about to open; focus is applied
    // in showEvent after the menu's own keyboard grab has been established.
    connect(this, &QMenu::aboutToShow, this, [this]{
        m_edit->clear();
    });
}

void SearchableMenu::showEvent(QShowEvent* e) {
    QMenu::showEvent(e);
    // Defer focus until after QMenu's keyboard grab is in place.
    QTimer::singleShot(0, m_edit, [this]{ m_edit->setFocus(); });
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
