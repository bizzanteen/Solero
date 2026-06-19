#include "FomodChoiceRecall.h"
#include <QJsonObject>
#include <QJsonArray>

namespace solero {

// Match the leniency the wizard/summary code uses elsewhere: compare names
// trimmed and case-insensitively.
static bool nameEq(const QString& a, const QString& b) {
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

FomodPreset buildFomodPreset(const FomodModule& mod, const QJsonObject& savedChoices) {
    FomodPreset out;
    const QJsonArray steps = savedChoices.value("steps").toArray();
    for (const QJsonValue& sv : steps) {
        const QJsonObject stepObj = sv.toObject();
        const QString stepName = stepObj.value("step").toString();

        // Find the matching step by name.
        int si = -1;
        for (int i = 0; i < mod.steps.size(); ++i)
            if (nameEq(mod.steps[i].name, stepName)) { si = i; break; }
        if (si < 0) continue; // step no longer present - skip its picks

        const QJsonArray selected = stepObj.value("selected").toArray();
        for (const QJsonValue& ov : selected) {
            const QString optName = ov.toString();
            // Find the (group, opt) for this option name within the step.
            bool matched = false;
            for (int gi = 0; gi < mod.steps[si].groups.size() && !matched; ++gi) {
                const FomodGroup& g = mod.steps[si].groups[gi];
                for (int oi = 0; oi < g.options.size(); ++oi) {
                    if (!nameEq(g.options[oi].name, optName)) continue;
                    const QString key = FomodEngine::selKey(si, gi, oi);
                    out.selection.insert(key, true);
                    out.priorKeys.insert(key);
                    matched = true;
                    break;
                }
            }
            // Unmatched names are silently skipped.
        }
    }
    return out;
}

} // namespace solero
