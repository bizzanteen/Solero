#pragma once

namespace solero {

// What an explicit "sort now" request should do.
//
// Sorting is always on offer - the action is never disabled - because the
// reasons someone wants a re-sort are mostly invisible from here. Editing a
// LOOT rule changes the sort result without touching the load order, so gating
// on "has the user reordered something" hid the button exactly when it was
// most wanted. A greyed-out menu item also cannot explain itself; the reasons
// below are resolved when the sort is requested, where they can be said out
// loud and, where it makes sense, overridden.
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
