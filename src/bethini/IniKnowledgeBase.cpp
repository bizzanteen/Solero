#include "IniKnowledgeBase.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace solero {

const IniKnowledgeBase& IniKnowledgeBase::instance() {
    static IniKnowledgeBase kb;
    return kb;
}

IniKnowledgeBase::IniKnowledgeBase() { load(); }

static IniSettingType parseType(const QString& t) {
    if (t == "int")   return IniSettingType::Int;
    if (t == "float") return IniSettingType::Float;
    if (t == "bool")  return IniSettingType::Bool;
    if (t == "enum")  return IniSettingType::Enum;
    return IniSettingType::String;
}

void IniKnowledgeBase::load() {
    QFile f(":/bethini-skyrimse.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    auto root = QJsonDocument::fromJson(f.readAll()).object();
    for (const auto& sv : root["settings"].toArray()) {
        auto s = sv.toObject();
        IniSetting setting;
        setting.section     = s["section"].toString();
        setting.key         = s["key"].toString();
        setting.file        = s["file"].toString();
        setting.label       = s["label"].toString();
        setting.description = s["description"].toString();
        setting.type        = parseType(s["type"].toString());
        if (s.contains("min"))  setting.min  = s["min"].toVariant();
        if (s.contains("max"))  setting.max  = s["max"].toVariant();
        if (s.contains("step")) setting.step = s["step"].toVariant();
        for (const auto& ev : s["enumValues"].toArray())
            setting.enumValues.append(ev.toString());
        auto presets = s["presets"].toObject();
        setting.presets.low     = presets.contains("Low")     ? presets["Low"].toVariant()     : QVariant();
        setting.presets.medium  = presets.contains("Medium")  ? presets["Medium"].toVariant()  : QVariant();
        setting.presets.high    = presets.contains("High")    ? presets["High"].toVariant()    : QVariant();
        setting.presets.ultra   = presets.contains("Ultra")   ? presets["Ultra"].toVariant()   : QVariant();
        setting.presets.bethini = presets.contains("BethINI") ? presets["BethINI"].toVariant() : QVariant();
        m_settings.append(setting);
    }
}

QStringList IniKnowledgeBase::sections() const {
    QStringList result;
    for (const auto& s : m_settings)
        if (!result.contains(s.section)) result.append(s.section);
    return result;
}

QList<IniSetting> IniKnowledgeBase::settingsForSection(const QString& section) const {
    QList<IniSetting> result;
    for (const auto& s : m_settings)
        if (s.section == section) result.append(s);
    return result;
}

const IniSetting* IniKnowledgeBase::find(const QString& section, const QString& key) const {
    for (const auto& s : m_settings)
        if (s.section == section && s.key == key) return &s;
    return nullptr;
}

} // namespace solero
