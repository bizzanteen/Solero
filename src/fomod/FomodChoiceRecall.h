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
//   { "steps": [ { "step": "<name>", "selected": ["<optName>", ...] }, ... ] }
// For each saved step, match it to a FomodStep by (trimmed, case-insensitive)
// name, then each selected option name to a FomodOption by name within that
// step. Matched (step,group,opt) indices get selection[selKey]=true and the key
// added to priorKeys. Names that don't resolve are skipped.
FomodPreset buildFomodPreset(const FomodModule& mod, const QJsonObject& savedChoices);

} // namespace solero
