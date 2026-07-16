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

// Pure mapping helper. `savedChoices` is the parsed {modId}.json:
//   { "steps": [ { "step": "<name>", "stepIndex": N, "selected": [...] }, ... ] }
// Each saved step maps to a FomodStep by stepIndex when present (newer logs), else
// by (trimmed, case-insensitive) name with each module step consumed at most once
// (modules repeat step names). Each selected option name maps to a FomodOption
// within that step, scanning groups in logged order. Matched indices get
// selection[selKey]=true and the key added to priorKeys; in pick-one groups only
// the LAST recorded pick is kept. Names that don't resolve are skipped.
FomodPreset buildFomodPreset(const FomodModule& mod, const QJsonObject& savedChoices);

} // namespace solero
