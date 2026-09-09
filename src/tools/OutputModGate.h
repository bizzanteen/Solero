#pragma once

namespace solero {

// Whether a run has to take the tool's own output mod out of the way first.
//
// A patcher reads the deployed Data (and, for PGPatcher, the generated fake-MO2
// instance built from the mod list). If its previous output is still enabled it
// re-ingests it and patches already-patched assets. MO2 users disable the output
// mod by hand; Solero has no VFS, so the disable only reaches Data after a deploy.
//
// Skipped when there is nothing staged yet - a first run has no prior output to
// trip over, and the deploy either side of it would be for nothing.
inline bool shouldClearOutputBeforeRun(bool presetWantsIt, bool outputModResolved,
                                       bool outputModEnabled, bool outputHasStagedFiles) {
    return presetWantsIt && outputModResolved && outputModEnabled && outputHasStagedFiles;
}

} // namespace solero
