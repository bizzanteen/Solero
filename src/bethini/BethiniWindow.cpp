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
#include <QPlainTextEdit>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

namespace solero {

// Is this the Resolution row (iSize W + iSize H, no built-in choices)?
static bool isResolutionRow(const BethiniRow& row) {
    if (!(row.type == "Dropdown" || row.type == "Combobox")) return false;
    if (!row.choices.isEmpty() || !row.settingChoices.isEmpty()) return false;
    if (row.iniKeys.size() != 2) return false;
    return row.iniKeys.at(0).key.toLower().contains("size w")
        && row.iniKeys.at(1).key.toLower().contains("size h");
}

// Standard resolutions per aspect ratio.
static QList<QPair<int,int>> resolutionsFor(const QString& aspect) {
    if (aspect == "16:10")
        return {{3840,2400},{2560,1600},{1920,1200},{1680,1050},{1600,1000},{1440,900},{1280,800}};
    return {{3840,2160},{2560,1440},{1920,1080},{1600,900},{1366,768},{1280,720}};
}

// Detect the native aspect ratio of the primary monitor ("16:10" or "16:9").
static QString detectNativeAspect() {
    if (auto* scr = QGuiApplication::primaryScreen()) {
        QSize n = scr->size();
        if (n.height() > 0) {
            double r = double(n.width()) / n.height();
            if (qAbs(r - 16.0/10.0) < qAbs(r - 16.0/9.0)) return "16:10";
        }
    }
    return "16:9";
}

BethiniWindow::BethiniWindow(QWidget* parent) : QWidget(parent) {
    setMinimumSize(1000, 720);
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
    auto* recBtn = new QPushButton("Apply Recommended Tweaks", this);
    recBtn->setToolTip("Apply BethINI's curated set of recommended INI tweaks for Skyrim SE");
    connect(recBtn, &QPushButton::clicked, this, &BethiniWindow::applyRecommendedTweaks);
    presetBar->addWidget(recBtn);
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

    // Green feedback line shown after applying a preset / recommended tweaks.
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #2e7d32; font-weight: bold;");
    m_statusLabel->setVisible(false);
    outer->addWidget(m_statusLabel);

    // Tabs
    auto* tabs = new QTabWidget(this);
    outer->addWidget(tabs);

    for (const auto& tab : data.tabs()) {
        auto* scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        auto* page = new QWidget(scroll);
        // Two balanced columns to save vertical space. Each group box is added
        // to whichever column is currently shorter (by accumulated row count).
        auto* columnsLayout = new QHBoxLayout(page);
        const int kColumns = 2;
        QList<QVBoxLayout*> columns;
        QList<int> colHeights;
        for (int c = 0; c < kColumns; ++c) {
            auto* col = new QVBoxLayout;
            columnsLayout->addLayout(col, 1);
            columns.append(col);
            colHeights.append(0);
        }

        for (const auto& group : tab.groups) {
            auto* box = new QGroupBox(group.name == "NoLabelFrame" ? QString() : group.name, page);
            auto* form = new QFormLayout(box);
            form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

            for (const auto& row : group.rows) {
                RowWidget rw;
                rw.row = row;

                // Special case: the Resolution row gets an aspect-ratio toggle
                // plus the resolution dropdown, in a single labeled row.
                if (isResolutionRow(rw.row)) {
                    auto* cell = new QWidget(box);
                    auto* h = new QHBoxLayout(cell);
                    h->setContentsMargins(0, 0, 0, 0);
                    auto* aspect = new QComboBox(cell);
                    aspect->addItems({"16:9", "16:10"});
                    aspect->setCurrentText(detectNativeAspect());
                    m_resCombo = new QComboBox(cell);
                    h->addWidget(aspect);
                    h->addWidget(m_resCombo, 1);

                    rw.widget = m_resCombo;
                    m_resRowIndex = m_rows.size();
                    m_rows.append(rw);
                    populateResolutions(aspect->currentText());
                    connect(aspect, &QComboBox::currentTextChanged, this,
                            [this](const QString& a){ populateResolutions(a); });

                    auto* lbl = new QLabel(rw.row.label, box);
                    lbl->setToolTip(rw.row.tooltip);
                    cell->setToolTip(rw.row.tooltip);
                    form->addRow(lbl, cell);
                    continue;
                }

                QWidget* w = nullptr;

                if (rw.row.type == "Checkbutton") {
                    w = new QCheckBox(box);
                } else if (rw.row.type == "Slider" || rw.row.type == "Spinbox") {
                    if (rw.row.decimals > 0) {
                        auto* s = new QDoubleSpinBox(box);
                        s->setDecimals(rw.row.decimals);
                        s->setMinimum(rw.row.hasRange ? rw.row.min : -1e6);
                        s->setMaximum(rw.row.hasRange ? rw.row.max :  1e6);
                        if (rw.row.step > 0) s->setSingleStep(rw.row.step);
                        w = s;
                    } else {
                        auto* s = new QSpinBox(box);
                        s->setMinimum(rw.row.hasRange ? int(rw.row.min) : -1000000);
                        s->setMaximum(rw.row.hasRange ? int(rw.row.max) :  1000000);
                        if (rw.row.step > 0) s->setSingleStep(int(rw.row.step));
                        w = s;
                    }
                } else if (rw.row.type == "Dropdown" || rw.row.type == "Combobox") {
                    auto* c = new QComboBox(box);
                    if (!rw.row.settingChoices.isEmpty())
                        for (const auto& cm : rw.row.settingChoices) c->addItem(cm.choice);
                    else
                        c->addItems(rw.row.choices);
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
            // Place this group in the shortest column.
            int shortest = 0;
            for (int c = 1; c < kColumns; ++c)
                if (colHeights[c] < colHeights[shortest]) shortest = c;
            columns[shortest]->addWidget(box);
            colHeights[shortest] += group.rows.size() + 2; // +2 for header/padding
        }
        for (auto* col : columns) col->addStretch();
        scroll->setWidget(page);
        tabs->addTab(scroll, tab.name);
    }

    // Advanced tab: raw editing of the profile's INI files.
    buildAdvancedTab(tabs);
}

void BethiniWindow::setProfile(Profile* profile) {
    m_profile = profile;
    for (auto& rw : m_rows) loadRow(rw);
    loadAdvancedFile();
}

void BethiniWindow::buildAdvancedTab(QTabWidget* tabs) {
    auto* page = new QWidget(tabs);
    auto* v = new QVBoxLayout(page);

    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel("File:"));
    m_advFileCombo = new QComboBox(page);
    m_advFileCombo->addItems({"Skyrim.ini", "SkyrimPrefs.ini", "SkyrimCustom.ini"});
    connect(m_advFileCombo, &QComboBox::currentTextChanged,
            this, &BethiniWindow::onAdvancedFileChanged);
    bar->addWidget(m_advFileCombo);
    bar->addStretch();
    auto* reload = new QPushButton("Reload", page);
    connect(reload, &QPushButton::clicked, this, &BethiniWindow::loadAdvancedFile);
    bar->addWidget(reload);
    auto* save = new QPushButton("Save to Profile", page);
    connect(save, &QPushButton::clicked, this, &BethiniWindow::onAdvancedSave);
    bar->addWidget(save);
    v->addLayout(bar);

    auto* hint = new QLabel(
        "Directly edit this profile's INI. Changes deploy to the game on next Deploy.", page);
    hint->setStyleSheet("color: gray;");
    v->addWidget(hint);

    m_advEdit = new QPlainTextEdit(page);
    QFont mono("Monospace");
    mono.setStyleHint(QFont::TypeWriter);
    m_advEdit->setFont(mono);
    m_advEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    v->addWidget(m_advEdit);

    tabs->addTab(page, "Advanced");
}

void BethiniWindow::loadAdvancedFile() {
    if (!m_profile || !m_advEdit || !m_advFileCombo) return;
    QString path = iniPathFor(m_advFileCombo->currentText());
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        m_advEdit->setPlainText(QString::fromUtf8(f.readAll()));
    else
        m_advEdit->setPlainText(QString("; %1 does not exist yet.\n"
            "; Editing here and saving will create it.\n").arg(m_advFileCombo->currentText()));
}

void BethiniWindow::onAdvancedFileChanged() {
    loadAdvancedFile();
}

void BethiniWindow::onAdvancedSave() {
    if (!m_profile || !m_advEdit || !m_advFileCombo) return;
    QString path = iniPathFor(m_advFileCombo->currentText());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(m_advEdit->toPlainText().toUtf8());
    // Reload the form widgets so the structured tabs reflect raw edits.
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

void BethiniWindow::populateResolutions(const QString& aspect) {
    if (m_resRowIndex < 0 || !m_resCombo) return;
    auto& rw = m_rows[m_resRowIndex];

    auto list = resolutionsFor(aspect);
    // Include the monitor's native mode if it matches this aspect and isn't listed.
    if (auto* scr = QGuiApplication::primaryScreen()) {
        QSize n = scr->size();
        QPair<int,int> nat{n.width(), n.height()};
        double r = n.height() ? double(n.width()) / n.height() : 0;
        bool natIs1610 = qAbs(r - 16.0/10.0) < qAbs(r - 16.0/9.0);
        if (((aspect == "16:10") == natIs1610) && !list.contains(nat))
            list.prepend(nat);
    }

    rw.row.settingChoices.clear();
    QSignalBlocker block(m_resCombo);
    m_resCombo->clear();
    for (const auto& rp : list) {
        BethiniChoiceMap cm;
        cm.choice = QString("%1x%2").arg(rp.first).arg(rp.second);
        cm.perKeyValues = { QStringList{QString::number(rp.first)},
                            QStringList{QString::number(rp.second)} };
        rw.row.settingChoices.append(cm);
        m_resCombo->addItem(cm.choice);
    }
    // Reselect the current INI resolution if it's in this aspect's list.
    int ci = matchChoiceFromIni(rw);
    if (ci >= 0) m_resCombo->setCurrentIndex(ci);
}

void BethiniWindow::loadRow(RowWidget& rw) {
    if (!m_profile || rw.row.iniKeys.isEmpty()) return;
    const auto& k0 = rw.row.iniKeys.first();
    QVariant val = readKey(k0);

    if (auto* cb = qobject_cast<QCheckBox*>(rw.widget)) {
        QString cur = val.toString();
        if (!rw.row.onValues.isEmpty())
            cb->setChecked(rw.row.onValues.first().contains(cur));
        else if (val.isValid())
            cb->setChecked(cur == "1" || val.toBool());
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
        if (!rw.row.onValues.isEmpty()) {
            for (int i = 0; i < rw.row.iniKeys.size(); ++i) {
                QStringList on  = i < rw.row.onValues.size()  ? rw.row.onValues.at(i)  : QStringList();
                QStringList off = i < rw.row.offValues.size() ? rw.row.offValues.at(i) : QStringList();
                QString v = cb->isChecked() ? (on.isEmpty()  ? "1" : on.first())
                                            : (off.isEmpty() ? "0" : off.first());
                writeKey(rw.row.iniKeys.at(i), v);
            }
        } else {
            writeKey(k0, cb->isChecked() ? "1" : "0");
        }
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
        cb->setChecked(!rw.row.onValues.isEmpty()
                           ? rw.row.onValues.first().contains(pv)
                           : pv == "1");
    else if (auto* spin = qobject_cast<QSpinBox*>(rw.widget))
        spin->setValue(pv.toInt());
    else if (auto* d = qobject_cast<QDoubleSpinBox*>(rw.widget))
        d->setValue(pv.toDouble());
    else if (auto* edit = qobject_cast<QLineEdit*>(rw.widget))
        edit->setText(pv);
}

void BethiniWindow::applyPreset(const QString& presetName) {
    if (!m_profile) { showStatus("No profile loaded"); return; }
    for (auto& rw : m_rows) applyPresetToRow(rw, presetName);
    QString shortName = presetName;
    shortName.remove("Bethini ");
    showStatus(QStringLiteral("✓ Applied preset: %1").arg(shortName));
}

// BethINI Pie's well-known "Recommended Tweaks" for Skyrim SE: a curated set of
// high-confidence, widely-recommended, safe INI writes. These go straight to the
// active profile's INI files via the same write path the editor uses; they do
// not depend on a UI row existing for the key.
void BethiniWindow::applyRecommendedTweaks() {
    if (!m_profile) { showStatus("No profile loaded"); return; }

    struct Tweak { const char* file; const char* section; const char* key; const char* value; };
    static const Tweak kTweaks[] = {
        // Disable mouse acceleration for 1:1 mouse input.
        {"Skyrim.ini",      "Controls", "bMouseAcceleration",      "0"},
        // Multi-threaded movement processing.
        {"Skyrim.ini",      "General",  "bMultiThreadMovement",    "1"},
        // Disable Papyrus debug logging (perf + reduces log spam).
        {"Skyrim.ini",      "Papyrus",  "bEnableLogging",          "0"},
        {"Skyrim.ini",      "Papyrus",  "bEnableTrace",            "0"},
        {"Skyrim.ini",      "Papyrus",  "bLoadDebugInformation",   "0"},
        // VSync on (engine standard key for SE).
        {"SkyrimPrefs.ini", "Display",  "iVSyncPresentInterval",   "1"},
        // Shadows render on grass.
        {"SkyrimPrefs.ini", "Display",  "bShadowsOnGrass",         "1"},
        // Allow loose-file mods.
        {"SkyrimPrefs.ini", "Launcher", "bEnableFileSelection",    "1"},
        // Disable story-manager logging spam.
        {"SkyrimPrefs.ini", "General",  "iStoryManagerLoggingEvent", "-1"},
    };

    int count = 0;
    for (const auto& t : kTweaks) {
        BethiniIniKey k;
        k.file    = QString::fromLatin1(t.file);
        k.section = QString::fromLatin1(t.section);
        k.key     = QString::fromLatin1(t.key);
        writeKey(k, QString::fromLatin1(t.value));
        ++count;
    }

    // Re-read the UI rows so widgets reflect the new INI values.
    reloadRows();
    showStatus(QStringLiteral("✓ Applied %1 recommended tweaks").arg(count));
}

void BethiniWindow::reloadRows() {
    for (auto& rw : m_rows) loadRow(rw);
    loadAdvancedFile();
}

void BethiniWindow::showStatus(const QString& message) {
    if (!m_statusLabel) return;
    m_statusLabel->setText(message);
    m_statusLabel->setVisible(true);
    QTimer::singleShot(4000, m_statusLabel, [lbl = m_statusLabel]{
        lbl->clear();
        lbl->setVisible(false);
    });
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
