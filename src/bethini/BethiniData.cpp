#include "BethiniData.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

namespace solero {

const BethiniData& BethiniData::instance() {
    static BethiniData s;
    return s;
}

BethiniData::BethiniData() { load(); }

// A settingChoices value entry is either a string or an array of strings.
static QStringList tokensFrom(const QJsonValue& v) {
    QStringList out;
    if (v.isArray()) {
        for (const auto& e : v.toArray()) out << e.toVariant().toString();
    } else {
        out << v.toVariant().toString();
    }
    return out;
}

void BethiniData::load() {
    QFile f(":/bethini-skyrimse.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    auto root = QJsonDocument::fromJson(f.readAll()).object();

    for (const auto& pv : root["presets"].toArray())
        m_presets << pv.toString();

    for (const auto& tv : root["tabs"].toArray()) {
        auto to = tv.toObject();
        BethiniTab tab;
        tab.name = to["name"].toString();
        for (const auto& gv : to["groups"].toArray()) {
            auto go = gv.toObject();
            BethiniGroup group;
            group.name = go["name"].toString();
            for (const auto& rv : go["rows"].toArray()) {
                auto ro = rv.toObject();
                BethiniRow row;
                row.label   = ro["label"].toString();
                row.type    = ro["type"].toString();
                row.tooltip = ro["tooltip"].toString();

                for (const auto& kv : ro["iniKeys"].toArray()) {
                    auto ko = kv.toObject();
                    BethiniIniKey key;
                    key.key     = ko["key"].toString();
                    key.section = ko["section"].toString();
                    key.file    = ko["file"].toString();
                    auto pres = ko["presets"].toObject();
                    for (auto it = pres.constBegin(); it != pres.constEnd(); ++it)
                        key.presets.insert(it.key(), it.value().toVariant().toString());
                    row.iniKeys.append(key);
                }

                for (const auto& cv : ro["choices"].toArray())
                    row.choices << cv.toString();

                auto sc = ro["settingChoices"].toObject();
                for (auto it = sc.constBegin(); it != sc.constEnd(); ++it) {
                    BethiniChoiceMap cm;
                    cm.choice = it.key();
                    for (const auto& entry : it.value().toArray())
                        cm.perKeyValues.append(tokensFrom(entry));
                    row.settingChoices.append(cm);
                }

                if (ro.contains("min") && ro.contains("max")) {
                    row.hasRange = true;
                    row.min = ro["min"].toDouble();
                    row.max = ro["max"].toDouble();
                }
                if (ro.contains("step"))     row.step = ro["step"].toDouble();
                if (ro.contains("decimals")) row.decimals = ro["decimals"].toInt();

                group.rows.append(row);
            }
            tab.groups.append(group);
        }
        m_tabs.append(tab);
    }
}

} // namespace solero
