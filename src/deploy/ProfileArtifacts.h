#pragma once
#include <QString>
#include <QStringList>

namespace solero { class Profile; }

namespace solero {

// The files Solero GENERATES for the game to read, as opposed to the mod files it
// links: the load order (Plugins.txt / loadorder.txt in the game's local appdata,
// i.e. inside the Proton prefix), and - when the profile owns them - its INIs and
// the per-profile save redirect in My Games.
//
// The game reads these files, never the profile, so anything that starts the game
// (or a tool that reads the load order) must write them first. They are derived
// entirely from the in-memory profile and cost milliseconds, so both deploy and
// launch write them unconditionally rather than only when the mod files change.

struct ArtifactWriteResult {
    bool ok = true;
    QStringList written;  // absolute paths actually written this call
    QStringList errors;   // human-readable failures, one per artifact
};

// Writes every generated artifact for `profile`. Targets come from AppConfig
// (localAppDataDir for the load order, documentsDir for the INIs); `gameDir`
// supplies the fallback location for each when AppConfig doesn't know it.
// Does NOT sort with LOOT and does NOT save the profile - callers own that.
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
