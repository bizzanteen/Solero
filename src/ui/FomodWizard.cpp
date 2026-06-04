#include "FomodWizard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QButtonGroup>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QPixmap>
#include <QFile>
#include <QDir>

namespace solero {

// Resolve a possibly wrong-case, backslash-style relative path under base. Returns "" if not found.
static QString resolveCI(const QString& base, const QString& relRaw) {
    QString rel = relRaw; rel.replace('\\', '/');
    QStringList parts = rel.split('/', Qt::SkipEmptyParts);
    QString cur = base;
    for (const QString& part : parts) {
        QDir d(cur);
        QString match;
        const auto entries = d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const QString& e : entries) if (e.compare(part, Qt::CaseInsensitive) == 0) { match = e; break; }
        if (match.isEmpty()) return {};
        cur = cur + "/" + match;
    }
    return cur;
}

FomodWizard::FomodWizard(FomodEngine* engine, const QString& extractDir, QWidget* parent)
    : QDialog(parent), m_engine(engine), m_extractDir(extractDir) {
    setWindowTitle("Install: " + engine->module().moduleName);
    resize(900, 600);
    auto* outer = new QVBoxLayout(this);

    m_stepTitle = new QLabel(this);
    QFont tf = m_stepTitle->font(); tf.setBold(true); tf.setPointSize(tf.pointSize()+2);
    m_stepTitle->setFont(tf);
    outer->addWidget(m_stepTitle);

    auto* mid = new QHBoxLayout;
    auto* optScroll = new QScrollArea(this);
    optScroll->setWidgetResizable(true);
    m_optionsHost = new QWidget(optScroll);
    optScroll->setWidget(m_optionsHost);
    mid->addWidget(optScroll, 1);

    auto* right = new QVBoxLayout;
    m_image = new QLabel(this);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setMinimumWidth(360);
    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    m_description->setAlignment(Qt::AlignTop);
    right->addWidget(m_image, 3);
    right->addWidget(m_description, 1);
    mid->addLayout(right, 1);
    outer->addLayout(mid, 1);

    auto* nav = new QHBoxLayout;
    m_backBtn = new QPushButton("< Back", this);
    m_nextBtn = new QPushButton("Next >", this);
    auto* cancel = new QPushButton("Cancel", this);
    nav->addWidget(cancel);
    nav->addStretch();
    nav->addWidget(m_backBtn);
    nav->addWidget(m_nextBtn);
    outer->addLayout(nav);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_backBtn, &QPushButton::clicked, this, &FomodWizard::onBack);
    connect(m_nextBtn, &QPushButton::clicked, this, &FomodWizard::onNext);

    rebuildVisibleSteps();
    if (m_visibleSteps.isEmpty()) { accept(); return; }
    showStep(0);
}

void FomodWizard::rebuildVisibleSteps() {
    auto present = [](const QString&){ return false; };
    m_visibleSteps.clear();
    for (int i = 0; i < m_engine->module().steps.size(); ++i)
        if (m_engine->isStepVisible(i, m_selection, present))
            m_visibleSteps.append(i);
}

void FomodWizard::showStep(int visibleIdx) {
    m_pos = qBound(0, visibleIdx, m_visibleSteps.size() - 1);
    int stepIdx = m_visibleSteps[m_pos];
    const FomodStep& step = m_engine->module().steps[stepIdx];
    m_stepTitle->setText(step.name);

    delete m_optionsHost->layout();
    qDeleteAll(m_optionsHost->findChildren<QWidget*>("", Qt::FindDirectChildrenOnly));
    auto* v = new QVBoxLayout(m_optionsHost);

    for (int gi = 0; gi < step.groups.size(); ++gi) {
        const FomodGroup& group = step.groups[gi];
        auto* box = new QGroupBox(group.name, m_optionsHost);
        auto* gv = new QVBoxLayout(box);
        bool exclusive = (group.type == GroupType::ExactlyOne || group.type == GroupType::AtMostOne);
        auto* bgroup = exclusive ? new QButtonGroup(box) : nullptr;
        if (bgroup) bgroup->setExclusive(true);

        for (int oi = 0; oi < group.options.size(); ++oi) {
            const FomodOption& opt = group.options[oi];
            QString key = FomodEngine::selKey(stepIdx, gi, oi);
            QAbstractButton* btn = exclusive
                ? static_cast<QAbstractButton*>(new QRadioButton(opt.name, box))
                : static_cast<QAbstractButton*>(new QCheckBox(opt.name, box));
            btn->setCheckable(true);
            if (bgroup) bgroup->addButton(btn);

            bool checked = m_selection.value(key, false);
            if (opt.baseType == OptionType::Required) { checked = true; btn->setEnabled(false); }
            if (opt.baseType == OptionType::NotUsable) { checked = false; btn->setEnabled(false); }
            if (!m_selection.contains(key) && opt.baseType == OptionType::Recommended) checked = true;
            btn->setChecked(checked);
            m_selection.insert(key, checked);

            connect(btn, &QAbstractButton::pressed, this, [this, opt]{
                if (!opt.description.isEmpty()) m_description->setText(opt.description);
                QString imgFull = opt.imagePath.isEmpty() ? QString() : resolveCI(m_extractDir, opt.imagePath);
                if (!imgFull.isEmpty()) {
                    QPixmap pm(imgFull);
                    m_image->setPixmap(pm.scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                } else m_image->clear();
            });
            connect(btn, &QAbstractButton::toggled, this, [this, key](bool on){
                m_selection.insert(key, on);
                onOptionToggled();
            });
            gv->addWidget(btn);
        }
        v->addWidget(box);
    }
    v->addStretch();
    updateNavButtons();
}

void FomodWizard::onOptionToggled() {
    rebuildVisibleSteps();
    updateNavButtons();
}

void FomodWizard::updateNavButtons() {
    m_backBtn->setEnabled(m_pos > 0);
    bool last = (m_pos >= m_visibleSteps.size() - 1);
    m_nextBtn->setText(last ? "Install" : "Next >");
}

void FomodWizard::onNext() {
    if (m_pos >= m_visibleSteps.size() - 1) { accept(); return; }
    showStep(m_pos + 1);
}

void FomodWizard::onBack() {
    if (m_pos > 0) showStep(m_pos - 1);
}

QList<FomodFile> FomodWizard::result() const {
    auto present = [](const QString&){ return false; };
    return m_engine->collectFiles(m_selection, present);
}

} // namespace solero
