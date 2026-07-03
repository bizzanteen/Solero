#pragma once
#include <QMenu>
#include <QList>
#include <functional>

class QLineEdit;
class QShowEvent;

namespace solero {

// A QMenu whose first item is a search line edit that live-filters the actions
// added via addItem(). Matching uses menuFilterMatch() (substring + loose
// subsequence). Pressing Enter activates the first visible item and closes the
// menu and all ancestor menus; the line edit is focused each time the menu is
// shown. Long lists scroll natively (QMenu handles overflow past the screen
// height).
class SearchableMenu : public QMenu {
    Q_OBJECT
public:
    explicit SearchableMenu(const QString& title, QWidget* parent = nullptr);

    // Add a filterable item. Only actions added this way participate in the
    // search; the embedded line edit is never hidden.
    QAction* addItem(const QString& text, std::function<void()> onTriggered);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void applyFilter(const QString& text);

    QLineEdit* m_edit = nullptr;
    QList<QAction*> m_items;
};

} // namespace solero
