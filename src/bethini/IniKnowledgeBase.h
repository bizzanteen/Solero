#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariant>

namespace solero {

enum class IniSettingType { Int, Float, Bool, String, Enum };

struct IniSettingPreset {
    QVariant low, medium, high, ultra, bethini;
};

struct IniSetting {
    QString section;
    QString key;
    QString file;       // "Skyrim.ini", "SkyrimPrefs.ini", or "SkyrimCustom.ini"
    QString label;
    QString description;
    IniSettingType type = IniSettingType::String;
    QVariant min, max, step;
    QStringList enumValues;
    IniSettingPreset presets;
};

class IniKnowledgeBase {
public:
    static const IniKnowledgeBase& instance();

    const QList<IniSetting>& settings() const { return m_settings; }
    QStringList sections() const;
    QList<IniSetting> settingsForSection(const QString& section) const;
    const IniSetting* find(const QString& section, const QString& key) const;

private:
    IniKnowledgeBase();
    QList<IniSetting> m_settings;
    void load();
};

} // namespace solero
