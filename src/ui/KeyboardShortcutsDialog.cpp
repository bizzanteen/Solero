#include "KeyboardShortcutsDialog.h"
#include <QMainWindow>
#include <QAction>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSet>
#include <QFont>

namespace solero {

namespace {
struct Shortcut { QString keys; QString description; };
struct Group    { QString title; QList<Shortcut> rows; };
} // namespace

KeyboardShortcutsDialog::KeyboardShortcutsDialog(QMainWindow* mainWindow, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Keyboard Shortcuts");
    resize(460, 520);

    // Application group: harvested from the live QActions so it can never drift
    //    from the menus/toolbar. Dedupe by key sequence (an action can appear via
    //    both a menu and a toolbar). '&' mnemonics and trailing ellipses stripped.
    Group app{ "Application", {} };
    QSet<QString> seen;
    if (mainWindow) {
        for (QAction* a : mainWindow->findChildren<QAction*>()) {
            const QKeySequence seq = a->shortcut();
            if (seq.isEmpty()) continue;
            QString text = a->text();
            text.remove(QChar('&'));
            text.remove(QChar(0x2026)); // trailing "…"
            text = text.trimmed();
            if (text.isEmpty()) continue;
            const QString keys = seq.toString(QKeySequence::NativeText);
            if (seen.contains(keys)) continue;
            seen.insert(keys);
            app.rows.append({keys, text});
        }
    }

    // Static bindings that aren't QActions. KEEP IN SYNC with where they're
    //    bound: the QShortcuts in MainWindow::setupCentralWidget and the Del/Space
    //    handlers in ModListView::keyPressEvent. Update both together.
    Group modList{ "Mod list", {
        {"Del",          "Delete selected mods"},
        {"Space",        "Toggle enabled on selection"},
        {"Ctrl+F",       "Focus the mod filter"},
        {"Ctrl+Z",       "Undo mod move"},
        {"Ctrl+Shift+Z", "Redo mod move"},
    }};
    Group plugins{ "Plugins", {
        {"Ctrl+Shift+F", "Focus the plugin search"},
    }};

    const QList<Group> groups{ app, modList, plugins };

    auto* table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Shortcut", "Action"});
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setHighlightSections(false);

    auto addGroupHeader = [&](const QString& title) {
        const int r = table->rowCount();
        table->insertRow(r);
        auto* item = new QTableWidgetItem(title);
        QFont f = item->font(); f.setBold(true); item->setFont(f);
        item->setFlags(Qt::ItemIsEnabled);
        table->setItem(r, 0, item);
        table->setItem(r, 1, new QTableWidgetItem());
        table->setSpan(r, 0, 1, 2);
    };
    auto addRow = [&](const Shortcut& s) {
        const int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r, 0, new QTableWidgetItem(s.keys));
        table->setItem(r, 1, new QTableWidgetItem(s.description));
    };
    for (const Group& g : groups) {
        if (g.rows.isEmpty()) continue;
        addGroupHeader(g.title);
        for (const Shortcut& s : g.rows) addRow(s);
    }
    table->resizeRowsToContents();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Shortcuts available in the main window:", this));
    layout->addWidget(table, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

} // namespace solero
