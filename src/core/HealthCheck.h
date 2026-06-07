#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QPair>

namespace solero {

class Profile;
class ConflictIndex;

// Severity ordering matters: higher value == worse, so worstSeverity() can pick
// the dominant one for the indicator's colour/icon.
enum class HealthSeverity { Info = 0, Warning = 1, Error = 2 };

enum class HealthCategory {
    MissingMaster,      // a plugin's master file isn't present (load will fail)
    MissingDependency,  // a mod's runtime dep is absent (SKSE / Address Library)
    FomodNeedsRerun,    // flag-driven FOMOD whose choices couldn't be recovered
    Conflict,           // informational: N files won/lost between mods
    DeployWarning,      // the last deploy reported a non-fatal warning
    DeployState,        // not deployed / pending redeploy
    PluginLimit,        // approaching / over the 255-plugin hard cap
};

// A single aggregated problem. targetModId / targetPlugin are optional jump-to
// hints (one of them, or neither for whole-profile issues). fixHint is a short
// human-readable suggestion shown in the panel when a remedy is known.
struct HealthIssue {
    HealthSeverity severity = HealthSeverity::Info;
    HealthCategory category = HealthCategory::Conflict;
    QString title;
    QString detail;
    QString targetModId;
    QString targetPlugin;
    QString fixHint;
};

// Live data the aggregator can't derive from Profile/ConflictIndex alone; the
// MainWindow fills this in from the same subsystems that already compute it.
struct HealthInputs {
    QHash<QString, QStringList> dependencyWarnings; // modId  -> warning strings
    QString lastDeployWarning;                      // DeployResult::warning, or empty
    bool deployed = false;                          // a deployment is currently live
    bool deployDirty = false;                       // staged changes pending redeploy
};

namespace health {

// Pure per-category builders
// Each takes already-extracted data so it can be unit-tested without the app.

// pluginMasters: {pluginFilename, missingMasterFilenames}. Only entries whose
// missing list is non-empty yield an Error issue (targetPlugin set).
QList<HealthIssue> missingMasterIssues(
    const QList<QPair<QString, QStringList>>& pluginMasters);

// warnings: modId -> warning strings; modNames: modId -> display name;
// modNexusIds: modId -> Nexus mod id (used to enrich the fix hint when known).
QList<HealthIssue> dependencyIssues(
    const QHash<QString, QStringList>& warnings,
    const QHash<QString, QString>& modNames,
    const QHash<QString, QString>& modNexusIds = {});

// mods: {modId, displayName} for each mod whose fomodStatus == "needs-rerun".
QList<HealthIssue> fomodRerunIssues(
    const QList<QPair<QString, QString>>& mods);

// One Info issue when conflictedPathCount > 0. involvedModNames is for the detail.
QList<HealthIssue> conflictIssues(int conflictedPathCount,
                                  const QStringList& involvedModNames);

QList<HealthIssue> deployWarningIssues(const QString& warning);

QList<HealthIssue> deployStateIssues(bool deployed, bool dirty);

// regularCount = enabled non-ESL plugins (those that consume a 00..FE slot).
// Error at/over the 255 cap, Warning when within 5 of it. lightCount is reported
// for context only.
QList<HealthIssue> pluginLimitIssues(int regularCount, int lightCount);

} // namespace health

// Worst severity across the list (Error > Warning > Info), or -1 when empty.
int worstSeverity(const QList<HealthIssue>& issues);

// Full aggregation: derives missing-masters / conflicts / FOMOD / plugin-count
// data from the Profile + ConflictIndex, folds in the live inputs, and returns
// every issue sorted worst-severity-first (stable within a severity).
QList<HealthIssue> collect(const Profile& profile, const ConflictIndex& conflicts,
                           const HealthInputs& inputs);

} // namespace solero
