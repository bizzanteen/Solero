#pragma once
#include <QString>

namespace solero {

// Fuzzy match used by SearchableMenu's live filter. Returns true when `needle`
// is empty, is a case-insensitive substring of `haystack`, or is a case-
// insensitive subsequence of it (every char of `needle` appears in `haystack`
// in order - so "wep" matches "01.4 WEAPONS"). Pure/header-only so it is unit
// testable without pulling in any widgets.
inline bool menuFilterMatch(const QString& needle, const QString& haystack) {
    if (needle.isEmpty()) return true;
    const QString n = needle.toLower();
    const QString h = haystack.toLower();
    if (h.contains(n)) return true;
    int i = 0;
    for (int j = 0; j < h.size() && i < n.size(); ++j)
        if (h.at(j) == n.at(i)) ++i;
    return i == n.size();
}

} // namespace solero
