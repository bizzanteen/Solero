#pragma once

namespace solero {

// What an explicit "sort now" request should do. The action is never disabled:
// editing a LOOT rule changes the sort result without touching the order, and a
// greyed-out item can't explain itself. The reasons below are resolved when the
// sort is asked for, where they can be said out loud or overridden.
enum class SortGate {
    Go,            // nothing in the way
    NeedsDeploy,   // libloot reads plugin headers out of the game's Data folder
    ConfirmLocked, // a lock is a preference an explicit request may override
};

inline SortGate sortGate(bool deployed, bool loadOrderLocked) {
    // Deploy first: confirming "sort anyway?" and then refusing anyway would be
    // a question the user cannot act on.
    if (!deployed) return SortGate::NeedsDeploy;
    if (loadOrderLocked) return SortGate::ConfirmLocked;
    return SortGate::Go;
}

} // namespace solero
