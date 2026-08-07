#pragma once
#include <QMenu>
#include <QList>
#include <functional>

class QLineEdit;
class QListWidget;
class QShowEvent;

namespace solero {

// A QMenu holding a search box above a capped, scrolling item list, so dozens of
// categories show as a compact dropdown rather than a screen-height column.
// Matching is menuFilterMatch. Enter takes the first visible item; either way the
// menu and its ancestors close.
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
