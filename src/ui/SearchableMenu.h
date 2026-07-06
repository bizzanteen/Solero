#pragma once
#include <QMenu>
#include <QList>
#include <functional>

class QLineEdit;
class QListWidget;
class QShowEvent;

namespace solero {

// A QMenu whose content is a search line edit above a BOUNDED, scrolling list of
// items (added via addItem()). The list is capped to a handful of visible rows
// with a scrollbar, so a long list (e.g. dozens of mod categories) shows as a
// compact, searchable dropdown instead of a full-screen-height column of actions.
// Matching uses menuFilterMatch() (substring + loose subsequence). Pressing Enter
// activates the first visible item; clicking one activates it. Either way the menu
// (and its ancestor menus) close. The line edit is focused each time it opens.
class SearchableMenu : public QMenu {
    Q_OBJECT
public:
    explicit SearchableMenu(const QString& title, QWidget* parent = nullptr);

    // Add a filterable item and the callback to run when it's chosen.
    void addItem(const QString& text, std::function<void()> onTriggered);

protected:
    void showEvent(QShowEvent* e) override;

private:
    void applyFilter(const QString& text);
    void resizeListToContents();
    void activateRow(int row);          // run row's callback + close the menu chain

    QLineEdit* m_edit = nullptr;
    QListWidget* m_list = nullptr;
    QList<std::function<void()>> m_callbacks; // parallel to the list's rows

    static constexpr int kMaxVisibleRows = 11; // cap before the list scrolls
};

} // namespace solero
