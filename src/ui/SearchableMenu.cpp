#include "SearchableMenu.h"
#include "MenuFilter.h"

#include <QLineEdit>
#include <QListWidget>
#include <QShowEvent>
#include <QTimer>
#include <QWidgetAction>
#include <QScrollBar>

namespace solero {

SearchableMenu::SearchableMenu(const QString& title, QWidget* parent)
    : QMenu(title, parent) {
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(QStringLiteral("Search categories") + QChar(0x2026));
    m_edit->setClearButtonEnabled(true);
    m_edit->setFocusPolicy(Qt::StrongFocus);

    auto* waEdit = new QWidgetAction(this);
    waEdit->setDefaultWidget(m_edit);
    addAction(waEdit);

    // The item list: bounded height + a scrollbar so a long category list stays a
    // compact dropdown rather than a full-screen column.
    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    auto* waList = new QWidgetAction(this);
    waList->setDefaultWidget(m_list);
    addAction(waList);

    connect(m_edit, &QLineEdit::textChanged, this,
            [this](const QString& t){ applyFilter(t); });

    // Enter activates the first visible row.
    connect(m_edit, &QLineEdit::returnPressed, this, [this]{
        for (int r = 0; r < m_list->count(); ++r)
            if (!m_list->item(r)->isHidden()) { activateRow(r); return; }
    });
    // A single click on a row activates it (menus feel click-to-pick).
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it){
        activateRow(m_list->row(it));
    });

    // Reset the filter each time the menu opens; focus is applied in showEvent
    // after the menu's own keyboard grab has been established.
    connect(this, &QMenu::aboutToShow, this, [this]{
        m_edit->clear();
        applyFilter(QString());
    });
}

void SearchableMenu::showEvent(QShowEvent* e) {
    QMenu::showEvent(e);
    resizeListToContents();
    // Defer focus until after QMenu's keyboard grab is in place.
    QTimer::singleShot(0, m_edit, [this]{ m_edit->setFocus(); });
}

void SearchableMenu::addItem(const QString& text, std::function<void()> onTriggered) {
    new QListWidgetItem(text, m_list);
    m_callbacks.append(std::move(onTriggered));
}

void SearchableMenu::applyFilter(const QString& text) {
    for (int r = 0; r < m_list->count(); ++r) {
        QListWidgetItem* it = m_list->item(r);
        it->setHidden(!menuFilterMatch(text, it->text()));
    }
    resizeListToContents();
}

void SearchableMenu::resizeListToContents() {
    if (!m_list->count()) return;
    int visible = 0;
    for (int r = 0; r < m_list->count(); ++r)
        if (!m_list->item(r)->isHidden()) ++visible;

    const int rowH = m_list->sizeHintForRow(0) > 0 ? m_list->sizeHintForRow(0) : 20;
    const int rows = qBound(1, visible, kMaxVisibleRows);
    m_list->setFixedHeight(rows * rowH + 2 * m_list->frameWidth());

    // Width: fit the widest item (plus scrollbar + padding), capped so a long
    // category name can't blow the menu across the screen.
    int wide = m_list->sizeHintForColumn(0);
    if (visible > kMaxVisibleRows)
        wide += m_list->verticalScrollBar()->sizeHint().width();
    m_list->setMinimumWidth(qMin(qMax(wide + 24, 160), 420));
}

void SearchableMenu::activateRow(int row) {
    if (row < 0 || row >= m_callbacks.size()) return;
    const auto cb = m_callbacks.at(row);
    // Close this submenu and every ancestor menu before running the callback (it
    // may pop its own dialogs / mutate the model).
    QMenu* m = this;
    while (m) { m->close(); m = qobject_cast<QMenu*>(m->parentWidget()); }
    if (cb) cb();
}

} // namespace solero
