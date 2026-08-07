#pragma once
#include <QString>
#include <QStringList>

namespace solero { class Profile; }

namespace solero {

// The files Solero generates for the game to read, rather than the mod files it
// links: the load order in the prefix's local appdata, and the profile's INIs and
// save redirect in My Games.
//
// The game reads these, never the profile, so anything that starts the game writes
// them first. They cost milliseconds, so deploy and launch both write them
// unconditionally.

struct ArtifactWriteResult {
    bool ok = true;
    QStringList written;  // absolute paths actually written this call
    // Artifacts that are in place but were deliberately left untouched (see the
    // empty-plugin-list guard below). Callers that track artifacts must treat these
    // as current, not stale - the deploy sweep would otherwise delete them.
    QStringList inPlace;
    QStringList errors;   // human-readable failures, one per artifact
};

// Writes every generated artifact. Targets come from AppConfig, with gameDir as
// the fallback. Does not sort with LOOT and does not save the profile. Refuses to
// overwrite a non-empty Plugins.txt from an empty plugin list.
ArtifactWriteResult writeProfileArtifacts(Profile& profile, const QString& gameDir);

// Where writeProfileArtifacts() puts Plugins.txt / loadorder.txt and the INIs.
QString loadOrderDir(const QString& gameDir);
QString iniDir(const QString& gameDir);

// Safety net: re-reads the Plugins.txt the game will actually load and compares it
// to the profile's own order. Returns an empty string when they agree; otherwise a
// short human-readable description of the first difference. A missing/unreadable
// live file counts as a mismatch - the game would fall back to its own ordering.
QString liveLoadOrderMismatch(const Profile& profile, const QString& gameDir);

} // namespace solero
