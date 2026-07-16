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
    // Modules often repeat step names (conditionally-visible twins), so matching
    // by name alone piled several installs' picks into the first same-named step
    // and marked more than one option "previously chosen" in a pick-one group.
    // Newer logs carry the exact stepIndex; for older logs each module step is
    // consumed at most once so same-named steps map 1:1 in order.
    QSet<int> used;
    for (const QJsonValue& sv : steps) {
        const QJsonObject stepObj = sv.toObject();
        const QString stepName = stepObj.value("step").toString();

        int si = -1;
        const int idx = stepObj.value("stepIndex").toInt(-1);
        if (idx >= 0 && idx < mod.steps.size() && nameEq(mod.steps[idx].name, stepName))
            si = idx;
        if (si < 0) {
            for (int i = 0; i < mod.steps.size(); ++i)
                if (!used.contains(i) && nameEq(mod.steps[i].name, stepName)) { si = i; break; }
        }
        if (si < 0) continue; // step no longer present - skip its picks
        used.insert(si);

        // Picks were logged in group order, and the same option name commonly
        // appears in several groups of one step ("1K"/"2K" per texture), so scan
        // forward from the last matched group first; fall back to a full scan
        // for logs from an older layout of the module.
        int gStart = 0;
        const QJsonArray selected = stepObj.value("selected").toArray();
        for (const QJsonValue& ov : selected) {
            const QString optName = ov.toString();
            int mgi = -1, moi = -1;
            for (int pass = 0; pass < 2 && mgi < 0; ++pass) {
                const int from = (pass == 0) ? gStart : 0;
                for (int gi = from; gi < mod.steps[si].groups.size() && mgi < 0; ++gi) {
                    const FomodGroup& g = mod.steps[si].groups[gi];
                    for (int oi = 0; oi < g.options.size(); ++oi)
                        if (nameEq(g.options[oi].name, optName)) { mgi = gi; moi = oi; break; }
                }
            }
            if (mgi < 0) continue; // option no longer present - skip

            const FomodGroup& g = mod.steps[si].groups[mgi];
            // A pick-one group logs at most one pick, so the next logged pick
            // must come from a LATER group - otherwise a same-named option in
            // this group would swallow it. Multi-pick groups can log several.
            gStart = (g.type == GroupType::ExactlyOne || g.type == GroupType::AtMostOne)
                         ? mgi + 1 : mgi;
            // A pick-one group holds a single "previously chosen": drop any
            // earlier mark so the LAST recorded pick wins. This also heals logs
            // that accumulated picks from several past installs.
            if (g.type == GroupType::ExactlyOne || g.type == GroupType::AtMostOne) {
                for (int po = 0; po < g.options.size(); ++po) {
                    const QString pk = FomodEngine::selKey(si, mgi, po);
                    out.selection.remove(pk);
                    out.priorKeys.remove(pk);
                }
            }
            const QString key = FomodEngine::selKey(si, mgi, moi);
            out.selection.insert(key, true);
            out.priorKeys.insert(key);
        }
    }
    return out;
}

} // namespace solero
