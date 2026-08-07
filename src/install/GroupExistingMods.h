#pragma once
#include "core/ModList.h"

namespace solero {

// One-time pass for mods installed before auto-grouping worked across separators.
// Nests every extra file of an already-installed Nexus mod under the first entry
// for that mod. Returns the number of mods moved.
//
// Only touches entries that share a nexusModId and are all still top-level: a
// group the user has already arranged, or broken up on purpose, is left alone.
int groupExistingMultiFileMods(ModList& list);

} // namespace solero
