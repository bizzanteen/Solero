#include "IniEditorPanel.h"
#include "IniKnowledgeBase.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSettings>

namespace solero {

IniEditorPanel::IniEditorPanel(QWidget* parent) : QWidget(parent) {
    buildUI();
}

static QString keyOf(const IniSetting& s) { return s.section + "::" + s.key; }

void IniEditorPanel::buildUI() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    // Preset buttons
    auto* presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel("Preset:"));
    for (const auto& preset : {"Low", "Medium", "High", "Ultra", "BethINI"}) {
        auto* btn = new QPushButton(preset, this);
        btn->setFixedHeight(22);
        connect(btn, &QPushButton::clicked, this, [this, p = QString(preset)]{ applyPreset(p); });
        presetRow->addWidget(btn);
    }
    presetRow->addStretch();
    auto* saveBtn = new QPushButton("Save to Profile", this);
    saveBtn->setFixedHeight(22);
    connect(saveBtn, &QPushButton::clicked, this, &IniEditorPanel::onSave);
    presetRow->addWidget(saveBtn);
    outer->addLayout(presetRow);

    // Search
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search settings…");
    outer->addWidget(m_search);

    // Scrollable form
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    auto* scrollWidget = new QWidget(m_scroll);
    m_form = new QFormLayout(scrollWidget);
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_scroll->setWidget(scrollWidget);
    outer->addWidget(m_scroll);

    const auto& kb = IniKnowledgeBase::instance();
    for (const auto& section : kb.sections()) {
        auto* sectionLabel = new QLabel(QString("<b>%1</b>").arg(section), scrollWidget);
        m_form->addRow(sectionLabel);
        for (const auto& s : kb.settingsForSection(section)) {
            QString k = keyOf(s);
            QWidget* widget = nullptr;
            if (s.type == IniSettingType::Bool) {
                widget = new QCheckBox(scrollWidget);
            } else if (s.type == IniSettingType::Int) {
                auto* spin = new QSpinBox(scrollWidget);
                spin->setMinimum(s.min.isValid() ? s.min.toInt() : -1000000);
                spin->setMaximum(s.max.isValid() ? s.max.toInt() :  1000000);
                if (s.step.isValid()) spin->setSingleStep(s.step.toInt());
                widget = spin;
            } else if (s.type == IniSettingType::Float) {
                auto* spin = new QDoubleSpinBox(scrollWidget);
                spin->setDecimals(4);
                spin->setMinimum(s.min.isValid() ? s.min.toDouble() : -1000000.0);
                spin->setMaximum(s.max.isValid() ? s.max.toDouble() :  1000000.0);
                if (s.step.isValid()) spin->setSingleStep(s.step.toDouble());
                widget = spin;
            } else if (s.type == IniSettingType::Enum) {
                auto* combo = new QComboBox(scrollWidget);
                combo->addItems(s.enumValues);
                widget = combo;
            } else {
                widget = new QLineEdit(scrollWidget);
            }
            widget->setToolTip(s.description);
            m_widgets[k] = widget;
            auto* lbl = new QLabel(s.label, scrollWidget);
            lbl->setToolTip(s.description);
            m_rowLabels[k] = lbl;
            m_form->addRow(lbl, widget);
        }
    }

    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& filter) {
        const auto& kb = IniKnowledgeBase::instance();
        for (const auto& s : kb.settings()) {
            QString k = keyOf(s);
            auto* w = m_widgets.value(k);
            auto* lbl = m_rowLabels.value(k);
            bool match = filter.isEmpty()
                || s.label.contains(filter, Qt::CaseInsensitive)
                || s.key.contains(filter, Qt::CaseInsensitive)
                || s.section.contains(filter, Qt::CaseInsensitive);
            if (w)   w->setVisible(match);
            if (lbl) lbl->setVisible(match);
        }
    });
}

void IniEditorPanel::setProfile(Profile* profile) {
    m_profile = profile;
    loadValuesFromProfile();
}

QString IniEditorPanel::iniPathFor(const IniSetting& s) const {
    if (!m_profile) return {};
    if (s.file == "Skyrim.ini")          return m_profile->skyrimIniPath();
    if (s.file == "SkyrimPrefs.ini")     return m_profile->skyrimPrefsPath();
    return m_profile->skyrimCustomPath();
}

QVariant IniEditorPanel::readIniValue(const IniSetting& s) const {
    QString path = iniPathFor(s);
    if (path.isEmpty()) return {};
    QSettings ini(path, QSettings::IniFormat);
    return ini.value(s.section + "/" + s.key);
}

void IniEditorPanel::writeIniValue(const IniSetting& s, const QVariant& value) {
    QString path = iniPathFor(s);
    if (path.isEmpty()) return;
    QSettings ini(path, QSettings::IniFormat);
    ini.setValue(s.section + "/" + s.key, value);
}

void IniEditorPanel::loadValuesFromProfile() {
    const auto& kb = IniKnowledgeBase::instance();
    for (const auto& s : kb.settings()) {
        auto* w = m_widgets.value(keyOf(s));
        if (!w) continue;
        QVariant val = readIniValue(s);
        if (!val.isValid()) continue;
        if (auto* cb = qobject_cast<QCheckBox*>(w))         cb->setChecked(val.toBool());
        else if (auto* spin = qobject_cast<QSpinBox*>(w))   spin->setValue(val.toInt());
        else if (auto* d = qobject_cast<QDoubleSpinBox*>(w))d->setValue(val.toDouble());
        else if (auto* combo = qobject_cast<QComboBox*>(w)) combo->setCurrentText(val.toString());
        else if (auto* edit = qobject_cast<QLineEdit*>(w))  edit->setText(val.toString());
    }
}

void IniEditorPanel::applyPreset(const QString& presetName) {
    const auto& kb = IniKnowledgeBase::instance();
    for (const auto& s : kb.settings()) {
        QVariant val;
        if (presetName == "Low")          val = s.presets.low;
        else if (presetName == "Medium")  val = s.presets.medium;
        else if (presetName == "High")    val = s.presets.high;
        else if (presetName == "Ultra")   val = s.presets.ultra;
        else if (presetName == "BethINI") val = s.presets.bethini;
        if (!val.isValid()) continue;
        auto* w = m_widgets.value(keyOf(s));
        if (!w) continue;
        if (auto* cb = qobject_cast<QCheckBox*>(w))          cb->setChecked(val.toBool());
        else if (auto* spin = qobject_cast<QSpinBox*>(w))    spin->setValue(val.toInt());
        else if (auto* d = qobject_cast<QDoubleSpinBox*>(w)) d->setValue(val.toDouble());
        else if (auto* combo = qobject_cast<QComboBox*>(w))  combo->setCurrentText(val.toString());
        else if (auto* edit = qobject_cast<QLineEdit*>(w))   edit->setText(val.toString());
    }
}

void IniEditorPanel::onSave() {
    if (!m_profile) return;
    const auto& kb = IniKnowledgeBase::instance();
    for (const auto& s : kb.settings()) {
        auto* w = m_widgets.value(keyOf(s));
        if (!w) continue;
        QVariant val;
        if (auto* cb = qobject_cast<QCheckBox*>(w))          val = cb->isChecked() ? 1 : 0;
        else if (auto* spin = qobject_cast<QSpinBox*>(w))    val = spin->value();
        else if (auto* d = qobject_cast<QDoubleSpinBox*>(w)) val = d->value();
        else if (auto* combo = qobject_cast<QComboBox*>(w))  val = combo->currentText();
        else if (auto* edit = qobject_cast<QLineEdit*>(w))   val = edit->text();
        if (val.isValid()) writeIniValue(s, val);
    }
}

} // namespace solero
