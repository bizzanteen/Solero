#pragma once
#include <QWidget>
#include <QStringList>

class QToolButton;
class QLabel;

namespace solero {
class Profile;
class ModInfoWidget;

class BottomPanel : public QWidget {
    Q_OBJECT
public:
    explicit BottomPanel(QWidget* parent = nullptr);
    void setProfile(Profile* profile);

    // Show/refresh the inline "Checking updates… N/M" text on the header row.
    // done < 0 (or done >= total) hides it. Right-justified and dimmed.
    void setUpdateProgress(int done, int total);

signals:
    // Forwarded from the Mod Info panel when a mod note is edited + saved, so the
    // mod list can refresh its note indicator.
    void noteChanged();

public slots:
    // Populate the Mod Info content for the first selected mod (or clear if empty).
    void onModsSelected(const QStringList& ids);

private:
    void setExpanded(bool expanded);

    QToolButton*     m_collapseBtn = nullptr;
    QLabel*          m_updateProgress = nullptr; // inline "Checking updates… N/M"
    ModInfoWidget*   m_modInfo     = nullptr;
    Profile*         m_profile     = nullptr;
    int              m_headerHeight = 24; // collapsed max-height (header only)
};
}
