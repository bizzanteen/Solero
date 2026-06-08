#include "HealthCheck.h"
#include "Profile.h"
#include "deploy/ConflictIndex.h"
#include <QSet>
#include <algorithm>

namespace solero {
namespace health {

QList<HealthIssue> missingMasterIssues(
    const QList<QPair<QString, QStringList>>& pluginMasters) {
    QList<HealthIssue> out;
    for (const auto& pm : pluginMasters) {
        if (pm.second.isEmpty()) continue;
        HealthIssue i;
        i.severity     = HealthSeverity::Error;
        i.category     = HealthCategory::MissingMaster;
        i.targetPlugin = pm.first;
        i.title        = QStringLiteral("%1 is missing masters").arg(pm.first);
        i.detail       = QStringLiteral("Required master(s) not present: %1")
                             .arg(pm.second.join(QStringLiteral(", ")));
        i.fixHint      = QStringLiteral(
            "Install/enable the mod that provides these plugins, or disable %1.")
                             .arg(pm.first);
        out.append(i);
    }
    return out;
}

QList<HealthIssue> dependencyIssues(
    const QHash<QString, QStringList>& warnings,
    const QHash<QString, QString>& modNames,
    const QHash<QString, QString>& modNexusIds) {
    QList<HealthIssue> out;
    // Stable output: iterate modNames order isn't defined for a hash, so sort by
    // mod id for a deterministic list (tests + a steady panel order).
    QStringList ids = warnings.keys();
    ids.sort();
    for (const QString& id : ids) {
        const QStringList warns = warnings.value(id);
        if (warns.isEmpty()) continue;
        const QString name = modNames.value(id, id);
        HealthIssue i;
        i.severity    = HealthSeverity::Warning;
        i.category    = HealthCategory::MissingDependency;
        i.targetModId = id;
        i.title       = QStringLiteral("%1 has an unmet dependency").arg(name);
        i.detail      = warns.join(QStringLiteral("\n"));
        const QString nexus = modNexusIds.value(id);
        i.fixHint = nexus.isEmpty()
            ? QStringLiteral("Install/enable the required dependency above.")
            : QStringLiteral("Install/enable the required dependency above "
                             "(this mod is Nexus id %1).").arg(nexus);
        out.append(i);
    }
    return out;
}

QList<HealthIssue> conflictIssues(int conflictedPathCount,
                                  const QStringList& involvedModNames) {
    QList<HealthIssue> out;
    if (conflictedPathCount <= 0) return out;
    HealthIssue i;
    i.severity = HealthSeverity::Info;
    i.category = HealthCategory::Conflict;
    i.title    = QStringLiteral("%1 file conflict(s) resolved by load order")
                     .arg(conflictedPathCount);
    if (involvedModNames.isEmpty()) {
        i.detail = QStringLiteral(
            "Multiple mods provide the same files; the higher-priority mod wins. "
            "This is informational ")
            + QChar('-') // - em dash
            + QStringLiteral(" review the Conflicts tab if unsure.");
    } else {
        i.detail = QStringLiteral(
            "Mods involved: %1. The higher-priority mod wins each file; review "
            "the Conflicts tab if a different winner is wanted.")
                       .arg(involvedModNames.join(QStringLiteral(", ")));
    }
    out.append(i);
    return out;
}

QList<HealthIssue> deployWarningIssues(const QString& warning) {
    QList<HealthIssue> out;
    if (warning.trimmed().isEmpty()) return out;
    HealthIssue i;
    i.severity = HealthSeverity::Warning;
    i.category = HealthCategory::DeployWarning;
    i.title    = QStringLiteral("Last deploy reported a warning");
    i.detail   = warning;
    out.append(i);
    return out;
}

QList<HealthIssue> deployStateIssues(bool deployed, bool dirty) {
    QList<HealthIssue> out;
    if (!deployed) {
        HealthIssue i;
        i.severity = HealthSeverity::Info;
        i.category = HealthCategory::DeployState;
        i.title    = QStringLiteral("Profile is not deployed");
        i.detail   = QStringLiteral(
            "Mods aren't linked into the game directory yet. Deploy (Ctrl+D) "
            "before launching the game.");
        i.fixHint  = QStringLiteral("Click Deploy (Ctrl+D).");
        out.append(i);
    } else if (dirty) {
        HealthIssue i;
        i.severity = HealthSeverity::Info;
        i.category = HealthCategory::DeployState;
        i.title    = QStringLiteral("Changes pending redeploy");
        i.detail   = QStringLiteral(
            "The mod/load order changed since the last deploy. Redeploy to apply "
            "them to the game directory.");
        i.fixHint  = QStringLiteral("Click Deploy (Ctrl+D) to redeploy.");
        out.append(i);
    }
    return out;
}

QList<HealthIssue> pluginLimitIssues(int regularCount, int lightCount) {
    QList<HealthIssue> out;
    constexpr int kCap = 255; // load-order slots 00..FE; FF is reserved
    if (regularCount > kCap) {
        HealthIssue i;
        i.severity = HealthSeverity::Error;
        i.category = HealthCategory::PluginLimit;
        i.title    = QStringLiteral("Over the plugin limit (%1 / %2)")
                         .arg(regularCount).arg(kCap);
        i.detail   = QStringLiteral(
            "%1 full plugins are enabled but only %2 fit. Disable or merge some, "
            "or flag suitable plugins as light (ESL). %3 light plugin(s) don't "
            "count against the cap.")
                         .arg(regularCount).arg(kCap).arg(lightCount);
        out.append(i);
    } else if (regularCount >= kCap - 5) {
        HealthIssue i;
        i.severity = HealthSeverity::Warning;
        i.category = HealthCategory::PluginLimit;
        i.title    = QStringLiteral("Approaching the plugin limit (%1 / %2)")
                         .arg(regularCount).arg(kCap);
        i.detail   = QStringLiteral(
            "Close to the %1 full-plugin cap. Consider flagging plugins as light "
            "(ESL). %2 light plugin(s) already don't count against the cap.")
                         .arg(kCap).arg(lightCount);
        out.append(i);
    }
    return out;
}

} // namespace health

int worstSeverity(const QList<HealthIssue>& issues) {
    int worst = -1;
    for (const auto& i : issues)
        worst = std::max(worst, static_cast<int>(i.severity));
    return worst;
}

QList<HealthIssue> collect(const Profile& profile, const ConflictIndex& conflicts,
                           const HealthInputs& inputs) {
    QList<HealthIssue> all;

    // Missing masters (derived from the reconciled plugin list)
    const PluginList& pl = profile.pluginList();
    QSet<QString> present;
    for (int i = 0; i < pl.count(); ++i)
        present.insert(pl.at(i).filename.toLower());
    QList<QPair<QString, QStringList>> pluginMasters;
    int regular = 0, light = 0;
    for (int i = 0; i < pl.count(); ++i) {
        const PluginEntry& p = pl.at(i);
        if (p.enabled) (p.isLight ? light : regular)++;
        // Disabled plugins won't load, so their missing masters aren't a problem.
        if (!p.enabled) continue;
        if (p.masters.isEmpty()) continue;
        QStringList missing;
        for (const QString& m : p.masters)
            if (!present.contains(m.toLower())) missing << m;
        if (!missing.isEmpty())
            pluginMasters.append({p.filename, missing});
    }
    all += health::missingMasterIssues(pluginMasters);

    // Missing dependencies (from DependencyChecker, passed in)
    QHash<QString, QString> modNames, modNexusIds;
    const ModList& ml = profile.modList();
    for (const auto& e : ml) {
        if (e.type != EntryType::Mod) continue;
        modNames.insert(e.id, e.name);
        if (!e.nexusModId.isEmpty()) modNexusIds.insert(e.id, e.nexusModId);
    }
    all += health::dependencyIssues(inputs.dependencyWarnings, modNames, modNexusIds);

    // Deploy warning + state
    all += health::deployWarningIssues(inputs.lastDeployWarning);
    all += health::deployStateIssues(inputs.deployed, inputs.deployDirty);

    // Plugin limit
    all += health::pluginLimitIssues(regular, light);

    // Conflicts (informational, last)
    const QStringList paths = conflicts.conflictedPaths();
    if (!paths.isEmpty()) {
        QSet<QString> involvedIds;
        for (const QString& path : paths) {
            const QString w = conflicts.winnerOf(path);
            if (!w.isEmpty()) involvedIds.insert(w);
            for (const QString& l : conflicts.losersOf(path)) involvedIds.insert(l);
        }
        QStringList names;
        for (const QString& id : involvedIds)
            names << modNames.value(id, id);
        names.sort(Qt::CaseInsensitive);
        all += health::conflictIssues(paths.size(), names);
    }

    // Worst-severity first, preserving insertion order within a severity.
    std::stable_sort(all.begin(), all.end(),
        [](const HealthIssue& a, const HealthIssue& b) {
            return static_cast<int>(a.severity) > static_cast<int>(b.severity);
        });
    return all;
}

} // namespace solero
