#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QPair>

namespace solero {

// One INI key controlled by a row. presets maps a preset name
// ("Bethini Low" ...) to that key's value for the preset.
struct BethiniIniKey {
    QString key;
    QString section;
    QString file;   // Skyrim.ini / SkyrimPrefs.ini / SkyrimCustom.ini
    QHash<QString, QString> presets;
};

// settingChoices: for a Dropdown, each choice maps to a list (one entry per
// iniKey) of acceptable values. The first value in each entry is canonical
// (written on save); any value matches on load.
struct BethiniChoiceMap {
    QString choice;
    QList<QStringList> perKeyValues; // size == iniKeys.size()
};

struct BethiniRow {
    QString label;
    QString type;     // Dropdown, Combobox, Slider, Spinbox, Checkbutton, Entry, Color
    QString tooltip;
    QList<BethiniIniKey> iniKeys;

    QStringList choices;               // for Dropdown/Combobox
    QList<BethiniChoiceMap> settingChoices;

    // For Checkbutton: per-key acceptable values when on / off (first is
    // canonical for writing). Not always 1/0 - e.g. bDisableAutoSave is inverted.
    QList<QStringList> onValues;
    QList<QStringList> offValues;

    bool   hasRange = false;
    double min = 0, max = 0, step = 1;
    int    decimals = 0;
};

struct BethiniGroup {
    QString name;
    QList<BethiniRow> rows;
};

struct BethiniTab {
    QString name;
    QList<BethiniGroup> groups;
};

class BethiniData {
public:
    static const BethiniData& instance();

    const QStringList& presets() const { return m_presets; }
    const QList<BethiniTab>& tabs() const { return m_tabs; }

private:
    BethiniData();
    void load();
    QStringList m_presets;
    QList<BethiniTab> m_tabs;
};

} // namespace solero
