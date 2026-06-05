#pragma once
#include <QWidget>
#include <QStringList>

class QToolButton;

namespace solero {
class Profile;
class ModInfoWidget;

class BottomPanel : public QWidget {
    Q_OBJECT
public:
    explicit BottomPanel(QWidget* parent = nullptr);
    void setProfile(Profile* profile);

public slots:
    // Populate the Mod Info content for the first selected mod (or clear if empty).
    void onModsSelected(const QStringList& ids);

private:
    void setExpanded(bool expanded);

    QToolButton*     m_collapseBtn = nullptr;
    ModInfoWidget*   m_modInfo     = nullptr;
    Profile*         m_profile     = nullptr;
    int              m_headerHeight = 24; // collapsed max-height (header only)
};
}
