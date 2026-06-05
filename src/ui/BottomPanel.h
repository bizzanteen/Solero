#pragma once
#include <QTabWidget>
#include <QStringList>

namespace solero {
class Profile;
class ModInfoWidget;

class BottomPanel : public QTabWidget {
    Q_OBJECT
public:
    explicit BottomPanel(QWidget* parent = nullptr);
    void setProfile(Profile* profile);

public slots:
    // Populate the Mod Info tab for the first selected mod (or clear if empty).
    void onModsSelected(const QStringList& ids);

private:
    ModInfoWidget*   m_modInfo    = nullptr;
    Profile*         m_profile    = nullptr;
};
}
