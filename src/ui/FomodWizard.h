#pragma once
#include <QDialog>
#include <QPixmap>
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

    FomodEngine* m_engine;
    QString m_extractDir;
    FomodEngine::Selection m_selection;
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
