#include "FomodWizard.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QRadioButton>
#include <QCheckBox>
#include <QButtonGroup>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QPixmap>
#include <QFile>
#include <QDir>
#include <QResizeEvent>
#include <QEvent>
#include <QMessageBox>

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

    auto* buttonBox = new QDialogButtonBox(this);
    m_backBtn = new QPushButton("< Back", this);
    m_nextBtn = new QPushButton("Next >", this);
    auto* cancel = new QPushButton("Cancel", this);
    buttonBox->addButton(cancel, QDialogButtonBox::RejectRole);
    buttonBox->addButton(m_backBtn, QDialogButtonBox::ActionRole);
    buttonBox->addButton(m_nextBtn, QDialogButtonBox::AcceptRole);
    outer->addWidget(buttonBox);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_backBtn, &QPushButton::clicked, this, &FomodWizard::onBack);
    connect(m_nextBtn, &QPushButton::clicked, this, &FomodWizard::onNext);

    rebuildVisibleSteps();
    if (m_visibleSteps.isEmpty()) { accept(); return; }
    showStep(0);
}

void FomodWizard::setPresetSelection(const FomodEngine::Selection& sel,
                                     const QSet<QString>& priorKeys) {
    // Seed the saved picks. showStep() honors m_selection.value(key) when building
    // each option, and the group-type handling (Required/NotUsable/All/ExactlyOne)
    // still normalizes anything that violates a group's constraint.
    for (auto it = sel.constBegin(); it != sel.constEnd(); ++it)
        m_selection.insert(it.key(), it.value());
    m_priorKeys = priorKeys;
    // A preset may flip a flag that makes a previously-hidden step visible, so
    // recompute visibility and re-render the current step with the new ticks/labels.
    rebuildVisibleSteps();
    if (!m_visibleSteps.isEmpty()) showStep(m_pos);
}

void FomodWizard::rebuildVisibleSteps() {
    m_visibleSteps.clear();
    for (int i = 0; i < m_engine->module().steps.size(); ++i)
        if (m_engine->isStepVisible(i, m_selection))
            m_visibleSteps.append(i);
}

void FomodWizard::clearHiddenStepSelections() {
    // Remove selection entries for any step that is currently not visible so
    // that flipping a flag off doesn't leave stale picks behind.
    const auto& steps = m_engine->module().steps;
    for (int si = 0; si < steps.size(); ++si) {
        if (m_engine->isStepVisible(si, m_selection)) continue;
        for (int gi = 0; gi < steps[si].groups.size(); ++gi)
            for (int oi = 0; oi < steps[si].groups[gi].options.size(); ++oi)
                m_selection.remove(FomodEngine::selKey(si, gi, oi));
    }
}

void FomodWizard::showStep(int visibleIdx) {
    m_pos = qBound(0, visibleIdx, m_visibleSteps.size() - 1);
    int stepIdx = m_visibleSteps[m_pos];
    const FomodStep& step = m_engine->module().steps[stepIdx];
    m_stepTitle->setText(step.name);

    delete m_optionsHost->layout();
    qDeleteAll(m_optionsHost->findChildren<QWidget*>("", Qt::FindDirectChildrenOnly));
    auto* v = new QVBoxLayout(m_optionsHost);

    QString firstSelectedImg;     // image of the first checked option on this step
    bool haveFirstSelectedImg = false;

    for (int gi = 0; gi < step.groups.size(); ++gi) {
        const FomodGroup& group = step.groups[gi];
        auto* box = new QGroupBox(group.name, m_optionsHost);
        auto* gv = new QVBoxLayout(box);
        bool exclusive = (group.type == GroupType::ExactlyOne || group.type == GroupType::AtMostOne);
        auto* bgroup = exclusive ? new QButtonGroup(box) : nullptr;
        if (bgroup) bgroup->setExclusive(true);

        // For SelectExactlyOne, guarantee exactly one is selected: track whether
        // anything ends up checked so we can force-select the first option.
        bool anyCheckedInGroup = false;
        QAbstractButton* firstBtnInGroup = nullptr;

        for (int oi = 0; oi < group.options.size(); ++oi) {
            const FomodOption& opt = group.options[oi];
            QString key = FomodEngine::selKey(stepIdx, gi, oi);
            // Evaluate the option's effective type against current flags/files,
            // so conditional (dependencyType) options can become Required /
            // NotUsable / Recommended mid-wizard.
            OptionType type = m_engine->effectiveType(opt, m_selection);
            // On reinstall, annotate options the user picked last time so they can
            // spot their earlier choices. Uses a real UTF-8 arrow, not a byte escape.
            QString label = opt.name;
            if (m_priorKeys.contains(key))
                label += QString::fromUtf8(" \xE2\x86\x90 previously chosen");
            QAbstractButton* btn = exclusive
                ? static_cast<QAbstractButton*>(new QRadioButton(label, box))
                : static_cast<QAbstractButton*>(new QCheckBox(label, box));
            btn->setCheckable(true);
            if (bgroup) bgroup->addButton(btn);
            if (!firstBtnInGroup) firstBtnInGroup = btn;

            bool checked = m_selection.value(key, false);
            bool forceEnabledOff = false;
            if (type == OptionType::Required)  { checked = true;  forceEnabledOff = true; }
            if (type == OptionType::NotUsable) { checked = false; forceEnabledOff = true; }
            if (!m_selection.contains(key) && type == OptionType::Recommended) checked = true;

            // SelectAll: every option checked and disabled (can't deselect).
            if (group.type == GroupType::All) { checked = true; forceEnabledOff = true; }

            if (forceEnabledOff) btn->setEnabled(false);
            btn->setChecked(checked);
            m_selection.insert(key, checked);
            if (checked) anyCheckedInGroup = true;
            if (checked && !haveFirstSelectedImg && !opt.imagePath.isEmpty()) {
                firstSelectedImg = opt.imagePath; haveFirstSelectedImg = true;
            }

            btn->setProperty("fomodImg", opt.imagePath);
            btn->setProperty("fomodDesc", opt.description);
            btn->setAttribute(Qt::WA_Hover, true);
            btn->installEventFilter(this);
            connect(btn, &QAbstractButton::pressed, this, [this, opt]{
                if (!opt.description.isEmpty()) m_description->setText(opt.description);
                setImage(opt.imagePath);
            });
            connect(btn, &QAbstractButton::toggled, this, [this, key](bool on){
                m_selection.insert(key, on);
                onOptionToggled();
            });
            gv->addWidget(btn);
        }

        // SelectExactlyOne with nothing selected (no Required/Recommended):
        // force-select the first option to satisfy "exactly one".
        if (group.type == GroupType::ExactlyOne && !anyCheckedInGroup && firstBtnInGroup) {
            firstBtnInGroup->setChecked(true);
            m_selection.insert(FomodEngine::selKey(stepIdx, gi, 0), true);
        }
        v->addWidget(box);
    }
    v->addStretch();

    // Fix #7: show the image of the first selected option for this step.
    setImage(haveFirstSelectedImg ? firstSelectedImg : QString());

    updateNavButtons();
}

void FomodWizard::onOptionToggled() {
    rebuildVisibleSteps();
    clearHiddenStepSelections();
    updateNavButtons();
}

bool FomodWizard::validateCurrentStep() {
    int stepIdx = m_visibleSteps[m_pos];
    const FomodStep& step = m_engine->module().steps[stepIdx];
    for (int gi = 0; gi < step.groups.size(); ++gi) {
        const FomodGroup& group = step.groups[gi];
        int checkedCount = 0;
        for (int oi = 0; oi < group.options.size(); ++oi)
            if (m_selection.value(FomodEngine::selKey(stepIdx, gi, oi), false))
                ++checkedCount;

        const QString g = group.name.isEmpty() ? QStringLiteral("a group") : ("\"" + group.name + "\"");
        switch (group.type) {
            case GroupType::ExactlyOne:
                if (checkedCount != 1) {
                    QMessageBox::information(this, "Selection required",
                        "Please select exactly one option in " + g + ".");
                    return false;
                }
                break;
            case GroupType::AtLeastOne:
                if (checkedCount < 1) {
                    QMessageBox::information(this, "Selection required",
                        "Please select at least one option in " + g + ".");
                    return false;
                }
                break;
            case GroupType::AtMostOne:
                if (checkedCount > 1) {
                    QMessageBox::information(this, "Selection invalid",
                        "Please select at most one option in " + g + ".");
                    return false;
                }
                break;
            case GroupType::All:
            case GroupType::Any:
                break;
        }
    }
    return true;
}

void FomodWizard::updateNavButtons() {
    m_backBtn->setEnabled(m_pos > 0);
    bool last = (m_pos >= m_visibleSteps.size() - 1);
    m_nextBtn->setText(last ? "Install" : "Next >");
}

void FomodWizard::onNext() {
    if (!validateCurrentStep()) return;
    if (m_pos >= m_visibleSteps.size() - 1) { accept(); return; }
    showStep(m_pos + 1);
}

void FomodWizard::onBack() {
    if (m_pos > 0) showStep(m_pos - 1);
}

QList<FomodFile> FomodWizard::result() const {
    return m_engine->collectFiles(m_selection);
}

void FomodWizard::setImage(const QString& imagePath) {
    QString full = imagePath.isEmpty() ? QString() : resolveCI(m_extractDir, imagePath);
    if (full.isEmpty()) { m_currentPixmap = QPixmap(); m_image->clear(); return; }
    m_currentPixmap = QPixmap(full);
    if (m_currentPixmap.isNull()) { m_image->clear(); return; }
    m_image->setPixmap(m_currentPixmap.scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void FomodWizard::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    if (!m_currentPixmap.isNull())
        m_image->setPixmap(m_currentPixmap.scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool FomodWizard::eventFilter(QObject* obj, QEvent* e) {
    if (e->type() == QEvent::Enter) {
        QString desc = obj->property("fomodDesc").toString();
        QString img  = obj->property("fomodImg").toString();
        if (!desc.isEmpty()) m_description->setText(desc);
        setImage(img);
    }
    return QDialog::eventFilter(obj, e);
}

} // namespace solero
