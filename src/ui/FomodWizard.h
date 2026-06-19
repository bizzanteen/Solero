#pragma once
#include <QDialog>
#include <QPixmap>
#include <QSet>
#include "fomod/FomodEngine.h"

class QLabel;
class QPushButton;

namespace solero {

class FomodWizard : public QDialog {
    Q_OBJECT
public:
    FomodWizard(FomodEngine* engine, const QString& extractDir, QWidget* parent = nullptr);
    FomodEngine::Selection selection() const { return m_selection; }
    QList<FomodFile> result() const;

    // Seed the wizard with a previously-saved set of choices (e.g. on reinstall):
    // those boxes start ticked and the matching options are labeled as previously
    // chosen. The wizard's normal group constraints still normalize the result.
    void setPresetSelection(const FomodEngine::Selection& sel, const QSet<QString>& priorKeys);

protected:
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void rebuildVisibleSteps();
    void showStep(int visibleIdx);
    void onOptionToggled();
    void updateNavButtons();
    void onNext();
    void onBack();
    void setImage(const QString& imagePath);
    // Validate the group-type constraints of the currently shown step.
    // Returns true if OK to advance; otherwise shows a message and returns false.
    bool validateCurrentStep();
    // Drop selections belonging to steps that are no longer visible.
    void clearHiddenStepSelections();

    FomodEngine* m_engine;
    QString m_extractDir;
    FomodEngine::Selection m_selection;
    // selKeys that were previously chosen (reinstall) - their option labels get a
    // "previously chosen" suffix so the user can spot their earlier picks.
    QSet<QString> m_priorKeys;
    QList<int> m_visibleSteps;
    int m_pos = 0;

    QLabel* m_stepTitle;
    QWidget* m_optionsHost;
    QLabel* m_image;
    QLabel* m_description;
    QPushButton* m_backBtn;
    QPushButton* m_nextBtn;
    QPixmap m_currentPixmap;
};

} // namespace solero
