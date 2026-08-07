#pragma once
#include <QString>

namespace solero {

// Result of an ESL-eligibility analysis of a TES4 plugin. `eligible` is only true
// when every new record's FormID fits the ESL object-index range and the new-record
// count is within the ESL cap. `reason` carries a human explanation when refused.
struct EslEligibility {
    bool    eligible = false;
    QString reason;
    int     newRecordCount = 0;
};

// Reads and rewrites a plugin's TES4 record header flags. The only mutation is
// the 4-byte flags field at offset 8; record data is never touched, and the file
// is copied to <path>.bak-solero first.
class PluginFlagEditor {
public:
    // The ESL / "light master" record-header flag on the TES4 record.
    static constexpr quint32 kLightFlag = 0x00000200u;
    // Compressed-record flag (record header, offset 8) - informational only; the
    // FormID we analyze lives in the record HEADER, so compression doesn't block us.
    static constexpr quint32 kCompressedFlag = 0x00040000u;
    // ESL object-index ceiling: a new FormID's low 24 bits must be <= 0xFFF.
    static constexpr quint32 kEslMaxObjectIndex = 0x00000FFFu;
    // Maximum number of new records an ESL may introduce.
    static constexpr int kEslMaxNewRecords = 2048;

    // True if `path` begins with a valid "TES4" record signature.
    static bool isTes4(const QString& path);

    // Current state of the ESL flag on the TES4 header. Sets *ok=false (and returns
    // false) if the file isn't a readable TES4 plugin.
    static bool isLight(const QString& path, bool* ok = nullptr);

    // Walk the plugin's group/record structure and decide whether it can safely be
    // ESL-flagged. Fails SAFE: any parse anomaly (bad signature, truncation, a size
    // that runs past EOF, a FormID referencing an undefined master) yields
    // eligible=false with an explanatory reason rather than a guess.
    static EslEligibility checkEslEligible(const QString& path);

    // Set or clear the ESL flag in place, after backing the file up. Setting
    // re-runs checkEslEligible() and refuses if the plugin isn't eligible - the UI
    // gate is defence in depth, this is the hard one.
    static bool setLightFlag(const QString& path, bool set, QString* error = nullptr);
};

} // namespace solero
