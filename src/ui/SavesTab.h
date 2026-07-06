#pragma once
#include <QWidget>

class QTableWidget;
class QLabel;
class QCheckBox;

namespace solero {
class Profile;

// A read-only list of the Skyrim Saves directory. Parses each *.ess header for
// character name / level / location / date / save # and a thumbnail, and flags any
// save that references plugins missing from the active profile's load order (MO2's
// "this save needs plugins you don't have"). It never deletes, renames, or moves a
// save - the only action is a directory rescan (Refresh).
class SavesTab : public QWidget {
    Q_OBJECT
public:
    explicit SavesTab(QWidget* parent = nullptr);
    // Bind the active profile (for missing-plugin detection) and rescan.
    void setProfile(Profile* profile);
    // Rescan the Saves directory. Cheap on repeat: unchanged files (same mtime)
    // are served from a per-path parse cache.
    void refresh();

protected:
    // Rescan when the tab is shown, so saves written while playing appear without a
    // manual refresh (the mtime cache keeps this cheap).
    void showEvent(QShowEvent* e) override;

private:
    void rebuild();
    // MO2-style previewer: fill the right-hand panel from the selected save (large
    // screenshot + metadata + missing-plugin list), or show a placeholder.
    void updatePreview();

    QTableWidget* m_table = nullptr;
    QLabel*       m_countLabel = nullptr;
    QCheckBox*    m_localSavesCheck = nullptr; // per-profile saves toggle
    QLabel*       m_previewShot = nullptr;     // large screenshot
    QLabel*       m_previewInfo = nullptr;     // metadata (rich text)
    QLabel*       m_previewMissing = nullptr;  // "needs plugins you don't have"
    Profile*      m_profile = nullptr;
};

} // namespace solero
