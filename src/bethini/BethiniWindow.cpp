#include "BethiniWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QScrollArea>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSettings>

namespace solero {

BethiniWindow::BethiniWindow(QWidget* parent) : QWidget(parent) {
    buildUI();
}

QString BethiniWindow::iniPathFor(const QString& file) const {
    if (!m_profile) return {};
    if (file == "Skyrim.ini")      return m_profile->skyrimIniPath();
    if (file == "SkyrimPrefs.ini") return m_profile->skyrimPrefsPath();
    return m_profile->skyrimCustomPath();
}

QVariant BethiniWindow::readKey(const BethiniIniKey& k) const {
    QString path = iniPathFor(k.file);
    if (path.isEmpty()) return {};
    QSettings ini(path, QSettings::IniFormat);
    return ini.value(k.section + "/" + k.key);
}

void BethiniWindow::writeKey(const BethiniIniKey& k, const QVariant& v) {
    QString path = iniPathFor(k.file);
    if (path.isEmpty()) return;
    QSettings ini(path, QSettings::IniFormat);
    ini.setValue(k.section + "/" + k.key, v);
}

void BethiniWindow::buildUI() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);

    const auto& data = BethiniData::instance();

    // Preset bar
    auto* presetBar = new QHBoxLayout;
    presetBar->addWidget(new QLabel("<b>Preset:</b>"));
    for (const auto& preset : data.presets()) {
        // Display the short label (strip "Bethini " prefix for the button)
        QString shortName = preset;
        shortName.remove("Bethini ");
        auto* btn = new QPushButton(shortName, this);
        btn->setToolTip("Apply " + preset);
        connect(btn, &QPushButton::clicked, this, [this, preset]{ applyPreset(preset); });
        presetBar->addWidget(btn);
    }
    presetBar->addStretch();
    auto* searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText("Search…");
    searchEdit->setMaximumWidth(200);
    connect(searchEdit, &QLineEdit::textChanged, this, &BethiniWindow::onSearch);
    presetBar->addWidget(searchEdit);
    auto* saveBtn = new QPushButton("Save to Profile", this);
    connect(saveBtn, &QPushButton::clicked, this, &BethiniWindow::onSave);
    presetBar->addWidget(saveBtn);
    outer->addLayout(presetBar);

    // Tabs
    auto* tabs = new QTabWidget(this);
    outer->addWidget(tabs);

    for (const auto& tab : data.tabs()) {
        auto* scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        auto* page = new QWidget(scroll);
        auto* pageLayout = new QVBoxLayout(page);

        for (const auto& group : tab.groups) {
            auto* box = new QGroupBox(group.name == "NoLabelFrame" ? QString() : group.name, page);
            auto* form = new QFormLayout(box);
            form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

            for (const auto& row : group.rows) {
                RowWidget rw;
                rw.row = row;
                QWidget* w = nullptr;

                if (row.type == "Checkbutton") {
                    w = new QCheckBox(box);
                } else if (row.type == "Slider" || row.type == "Spinbox") {
                    if (row.decimals > 0) {
                        auto* s = new QDoubleSpinBox(box);
                        s->setDecimals(row.decimals);
                        s->setMinimum(row.hasRange ? row.min : -1e6);
                        s->setMaximum(row.hasRange ? row.max :  1e6);
                        if (row.step > 0) s->setSingleStep(row.step);
                        w = s;
                    } else {
                        auto* s = new QSpinBox(box);
                        s->setMinimum(row.hasRange ? int(row.min) : -1000000);
                        s->setMaximum(row.hasRange ? int(row.max) :  1000000);
                        if (row.step > 0) s->setSingleStep(int(row.step));
                        w = s;
                    }
                } else if (row.type == "Dropdown" || row.type == "Combobox") {
                    auto* c = new QComboBox(box);
                    if (!row.settingChoices.isEmpty())
                        for (const auto& cm : row.settingChoices) c->addItem(cm.choice);
                    else
                        c->addItems(row.choices);
                    w = c;
                } else { // Entry, Color, fallback
                    w = new QLineEdit(box);
                }

                w->setToolTip(row.tooltip);
                auto* lbl = new QLabel(row.label, box);
                lbl->setToolTip(row.tooltip);
                form->addRow(lbl, w);

                rw.widget = w;
                rw.container = w; // hide widget on search; label tracked via form
                m_rows.append(rw);
            }
            pageLayout->addWidget(box);
        }
        pageLayout->addStretch();
        scroll->setWidget(page);
        tabs->addTab(scroll, tab.name);
    }
}

void BethiniWindow::setProfile(Profile* profile) {
    m_profile = profile;
    for (auto& rw : m_rows) loadRow(rw);
}

int BethiniWindow::matchChoiceFromIni(const RowWidget& rw) const {
    // Find the settingChoices entry whose acceptable values all match the
    // current INI values for each key.
    for (int ci = 0; ci < rw.row.settingChoices.size(); ++ci) {
        const auto& cm = rw.row.settingChoices.at(ci);
        if (cm.perKeyValues.size() != rw.row.iniKeys.size()) continue;
        bool allMatch = true;
        for (int ki = 0; ki < rw.row.iniKeys.size(); ++ki) {
            QString cur = readKey(rw.row.iniKeys.at(ki)).toString();
            if (!cm.perKeyValues.at(ki).contains(cur)) { allMatch = false; break; }
        }
        if (allMatch) return ci;
    }
    return -1;
}

void BethiniWindow::writeChoice(const RowWidget& rw, int choiceIndex) {
    if (choiceIndex < 0 || choiceIndex >= rw.row.settingChoices.size()) return;
    const auto& cm = rw.row.settingChoices.at(choiceIndex);
    for (int ki = 0; ki < rw.row.iniKeys.size() && ki < cm.perKeyValues.size(); ++ki) {
        const auto& accepted = cm.perKeyValues.at(ki);
        if (!accepted.isEmpty())
            writeKey(rw.row.iniKeys.at(ki), accepted.first()); // canonical = first
    }
}

void BethiniWindow::loadRow(RowWidget& rw) {
    if (!m_profile || rw.row.iniKeys.isEmpty()) return;
    const auto& k0 = rw.row.iniKeys.first();
    QVariant val = readKey(k0);

    if (auto* cb = qobject_cast<QCheckBox*>(rw.widget)) {
        if (val.isValid()) cb->setChecked(val.toString() == "1" || val.toBool());
    } else if (auto* spin = qobject_cast<QSpinBox*>(rw.widget)) {
        if (val.isValid()) spin->setValue(val.toInt());
    } else if (auto* d = qobject_cast<QDoubleSpinBox*>(rw.widget)) {
        if (val.isValid()) d->setValue(val.toDouble());
    } else if (auto* combo = qobject_cast<QComboBox*>(rw.widget)) {
        if (!rw.row.settingChoices.isEmpty()) {
            int ci = matchChoiceFromIni(rw);
            if (ci >= 0) combo->setCurrentIndex(ci);
        } else if (val.isValid()) {
            int idx = combo->findText(val.toString());
            if (idx >= 0) combo->setCurrentIndex(idx);
        }
    } else if (auto* edit = qobject_cast<QLineEdit*>(rw.widget)) {
        if (val.isValid()) edit->setText(val.toString());
    }
}

void BethiniWindow::saveRow(RowWidget& rw) {
    if (!m_profile || rw.row.iniKeys.isEmpty()) return;
    const auto& k0 = rw.row.iniKeys.first();

    if (auto* cb = qobject_cast<QCheckBox*>(rw.widget)) {
        writeKey(k0, cb->isChecked() ? "1" : "0");
    } else if (auto* spin = qobject_cast<QSpinBox*>(rw.widget)) {
        writeKey(k0, spin->value());
    } else if (auto* d = qobject_cast<QDoubleSpinBox*>(rw.widget)) {
        writeKey(k0, d->value());
    } else if (auto* combo = qobject_cast<QComboBox*>(rw.widget)) {
        if (!rw.row.settingChoices.isEmpty())
            writeChoice(rw, combo->currentIndex());
        else
            writeKey(k0, combo->currentText());
    } else if (auto* edit = qobject_cast<QLineEdit*>(rw.widget)) {
        writeKey(k0, edit->text());
    }
}

void BethiniWindow::applyPresetToRow(RowWidget& rw, const QString& presetName) {
    if (rw.row.iniKeys.isEmpty()) return;

    if (auto* combo = qobject_cast<QComboBox*>(rw.widget)) {
        if (!rw.row.settingChoices.isEmpty()) {
            // Determine the preset's target value per key, then find the matching choice.
            for (int ci = 0; ci < rw.row.settingChoices.size(); ++ci) {
                const auto& cm = rw.row.settingChoices.at(ci);
                if (cm.perKeyValues.size() != rw.row.iniKeys.size()) continue;
                bool allMatch = true;
                bool anyPreset = false;
                for (int ki = 0; ki < rw.row.iniKeys.size(); ++ki) {
                    QString pv = rw.row.iniKeys.at(ki).presets.value(presetName);
                    if (pv.isEmpty()) { allMatch = false; break; }
                    anyPreset = true;
                    if (!cm.perKeyValues.at(ki).contains(pv)) { allMatch = false; break; }
                }
                if (anyPreset && allMatch) { combo->setCurrentIndex(ci); return; }
            }
        }
        return; // no preset data for this dropdown
    }

    QString pv = rw.row.iniKeys.first().presets.value(presetName);
    if (pv.isEmpty()) return; // setting unchanged across presets

    if (auto* cb = qobject_cast<QCheckBox*>(rw.widget))
        cb->setChecked(pv == "1");
    else if (auto* spin = qobject_cast<QSpinBox*>(rw.widget))
        spin->setValue(pv.toInt());
    else if (auto* d = qobject_cast<QDoubleSpinBox*>(rw.widget))
        d->setValue(pv.toDouble());
    else if (auto* edit = qobject_cast<QLineEdit*>(rw.widget))
        edit->setText(pv);
}

void BethiniWindow::applyPreset(const QString& presetName) {
    for (auto& rw : m_rows) applyPresetToRow(rw, presetName);
}

void BethiniWindow::onSave() {
    if (!m_profile) return;
    for (auto& rw : m_rows) saveRow(rw);
}

void BethiniWindow::onSearch(const QString& filter) {
    for (auto& rw : m_rows) {
        bool match = filter.isEmpty()
            || rw.row.label.contains(filter, Qt::CaseInsensitive)
            || rw.row.tooltip.contains(filter, Qt::CaseInsensitive);
        if (rw.widget) {
            rw.widget->setVisible(match);
            // hide the buddy label too
            if (auto* form = qobject_cast<QFormLayout*>(rw.widget->parentWidget()->layout())) {
                if (auto* lbl = form->labelForField(rw.widget)) lbl->setVisible(match);
            }
        }
    }
}

} // namespace solero
