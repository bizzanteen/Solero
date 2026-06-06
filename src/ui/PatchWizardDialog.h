#pragma once
#include "patch/PatchScanner.h"
#include <QDialog>
#include <QList>
#include <QStringList>

class QCheckBox;
class QVBoxLayout;
class QPushButton;

namespace solero {

class Profile;

// On-demand "Patch Wizard": scans installed FOMOD mods for optional options that
// are now applicable to the current load order but were not installed, and offers
// to install just those files retroactively.
class PatchWizardDialog : public QDialog {
    Q_OBJECT
public:
    explicit PatchWizardDialog(Profile* profile, QWidget* parent = nullptr);

signals:
    // Emitted after files are installed; carries the affected mod ids so the
    // caller can invalidate caches and mark the deployment dirty.
    void patchesInstalled(const QStringList& modIds);

private:
    void runScan();
    void buildList();
    void onInstallSelected();

    Profile* m_profile = nullptr;
    QList<PatchCandidate> m_candidates;
    QList<QCheckBox*> m_checks;     // parallel to m_candidates
    QVBoxLayout* m_listLayout = nullptr;
    QPushButton* m_installBtn = nullptr;
};

} // namespace solero
