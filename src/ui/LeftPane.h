#pragma once
#include <QTabWidget>
#include "core/Types.h"
namespace solero { class ModListView; class BethiniWindow; class ToolStore; class ToolPanel; }
namespace solero {
class LeftPane : public QTabWidget {
    Q_OBJECT
public:
    LeftPane(ModListView* mods, BethiniWindow* bethini, ToolStore* tools, QWidget* parent = nullptr);
    void rebuildToolTabs();   // re-create one tab per tool in the store
    void showMods();
signals:
    void runTool(const Executable& exe);
    void editTool(const QString& id);
    void removeTool(const QString& id);
    void addToolRequested();
private:
    ModListView* m_mods;
    BethiniWindow* m_bethini;
    ToolStore* m_tools;
};
}
