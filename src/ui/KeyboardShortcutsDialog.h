#pragma once
#include <QDialog>

class QMainWindow;

namespace solero {

// A read-only two-column reference of the app's keyboard shortcuts. The list is
// generated from reality: it walks the main window's QAction children (menu +
// toolbar) for their assigned shortcuts, then folds in a small static list of
// bindings that aren't QActions (the QShortcut-driven search focus / undo-redo
// and the ModListView Del/Space handlers). Grouped roughly App / Mod list /
// Plugins. Opened from Help ▸ Keyboard Shortcuts (F1).
class KeyboardShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit KeyboardShortcutsDialog(QMainWindow* mainWindow, QWidget* parent = nullptr);
};

} // namespace solero
