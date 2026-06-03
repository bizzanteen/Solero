#include "BottomPanel.h"
#include <QLabel>
namespace solero {
BottomPanel::BottomPanel(QWidget* parent) : QTabWidget(parent) {
    addTab(new QLabel("Select a mod to see info", this), "Mod Info");
    addTab(new QLabel("Select a mod to see conflicts", this), "Conflicts");
    addTab(new QLabel("INI Editor - Stage 2", this), "INI Editor");
    addTab(new QLabel("LOOT Rules - Stage 2", this), "LOOT Rules");
    setMaximumHeight(250);
}
}
