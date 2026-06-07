#pragma once
#include "patch/PatchScanner.h"
#include <QDialog>
#include <QList>
#include <QStringList>

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;

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
    void buildTree();
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onInstallSelected();
    void setAllChecked(bool checked);
    void applyFilter(const QString& text);
    void updateParentState(QTreeWidgetItem* parent);
    void updateInstallEnabled();

    Profile* m_profile = nullptr;
    QList<PatchCandidate> m_candidates;
    QTreeWidget* m_tree = nullptr;
    QLineEdit* m_filter = nullptr;
    QLabel* m_empty = nullptr;
    QPushButton* m_installBtn = nullptr;
    QPushButton* m_selectAllBtn = nullptr;
    QPushButton* m_selectNoneBtn = nullptr;
    bool m_updating = false;   // re-entrancy guard for check-state propagation
};

} // namespace solero
