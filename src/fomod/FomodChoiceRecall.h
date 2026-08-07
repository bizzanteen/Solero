#pragma once
#include "FomodEngine.h"
#include <QSet>
#include <QString>

class QJsonObject;

namespace solero {

// Result of mapping a previously-saved set of FOMOD choices (step name +
// selected option *names*) onto a (possibly changed) FomodModule, by index.
struct FomodPreset {
    FomodEngine::Selection selection; // selKey -> true for previously-chosen options
    QSet<QString>          priorKeys; // selKeys that were "previously chosen" (for labeling)
};

// Maps a parsed {modId}.json onto the module's steps:
//   { "steps": [ { "step": "<name>", "stepIndex": N, "selected": [...] }, ... ] }
// By stepIndex when present, else by name with each module step consumed once,
// since modules repeat step names. In pick-one groups only the last recorded pick
// survives. Names that don't resolve are skipped.
FomodPreset buildFomodPreset(const FomodModule& mod, const QJsonObject& savedChoices);

} // namespace solero
