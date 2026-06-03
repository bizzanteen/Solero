#pragma once
#include <QTabWidget>

namespace solero {
class Profile;
class LootRulesEditor;
class IniEditorPanel;

class BottomPanel : public QTabWidget {
    Q_OBJECT
public:
    explicit BottomPanel(QWidget* parent = nullptr);
    void setProfile(Profile* profile);

private:
    LootRulesEditor* m_lootEditor = nullptr;
    IniEditorPanel* m_iniEditor = nullptr;
};
}
