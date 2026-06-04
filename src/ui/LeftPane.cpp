#include "LeftPane.h"
#include "ModListView.h"
#include "bethini/BethiniWindow.h"
#include "tools/ToolStore.h"
#include "ToolPanel.h"
#include <QToolButton>
#include <QIcon>
namespace solero {

LeftPane::LeftPane(ModListView* mods, BethiniWindow* bethini, ToolStore* tools, QWidget* parent)
    : QTabWidget(parent), m_mods(mods), m_bethini(bethini), m_tools(tools) {
    addTab(mods, "Mods");
    addTab(bethini, "BethINI");
    setMovable(true);

    // Corner "Add Tool" button (avoids fake-tab snap-back complexity).
    auto* addBtn = new QToolButton(this);
    addBtn->setText("\xe2\x9e\x95 Tool");
    addBtn->setToolTip("Set up a new tool");
    connect(addBtn, &QToolButton::clicked, this, [this]{ emit addToolRequested(); });
    setCornerWidget(addBtn, Qt::TopRightCorner);

    rebuildToolTabs();
}

void LeftPane::rebuildToolTabs() {
    // Remove all tabs after index 1 (BethINI).
    while (count() > 2) {
        QWidget* w = widget(2);
        removeTab(2);
        delete w;
    }
    for (const auto& exe : m_tools->tools()) {
        auto* panel = new ToolPanel(exe, this);
        int idx = addTab(panel, exe.name);
        setTabIcon(idx, QIcon(exe.iconPath));
        connect(panel, &ToolPanel::runRequested, this, &LeftPane::runTool);
        connect(panel, &ToolPanel::editRequested, this, &LeftPane::editTool);
        connect(panel, &ToolPanel::removeRequested, this, &LeftPane::removeTool);
    }
}

void LeftPane::showMods() {
    setCurrentIndex(0);
}

}
