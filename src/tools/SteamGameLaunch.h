#pragma once
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// Launching the game ourselves through Proton breaks when Steam is a Flatpak: the
// client lives in its own PID namespace, so the PID the game registers with is one
// Steam can't see. It drops the app from its running list a second later, tears the
// game's IPC pipe down, and steamclient asserts mid-session. Letting Steam start
// the game keeps it inside Steam's own process tree, where tracking works.

namespace solero {

// "<steam>/steamapps/compatdata/489830" -> "489830". Empty when the leaf isn't an
// app id, so the caller can fall back rather than launch something random.
inline QString steamAppIdFromPrefix(const QString& winePrefix) {
    if (winePrefix.isEmpty()) return {};
    // cleanPath first: QFileInfo::fileName() is empty for a trailing slash.
    const QString leaf = QFileInfo(QDir::cleanPath(winePrefix)).fileName();
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    return digits.match(leaf).hasMatch() ? leaf : QString();
}

// Tells an already-running client to launch the app itself.
inline QStringList steamAppLaunchArgs(const QString& appId) {
    return {QStringLiteral("-applaunch"), appId};
}

enum class GameSession {
    Starting,     // asked Steam, game not up yet
    Running,
    Ended,        // was up, now gone
    NeverStarted, // waited past the start timeout and never saw it
};

// One poll tick while waiting on a Steam-launched game. `alive` is whether the game
// process is up right now, `everAlive` whether an earlier tick saw it. The start
// timeout only applies before the first sighting, so a long session never trips it.
inline GameSession gameSessionStep(bool alive, bool everAlive, qint64 waitedMs,
                                   qint64 startTimeoutMs) {
    if (alive) return GameSession::Running;
    if (everAlive) return GameSession::Ended;
    return waitedMs >= startTimeoutMs ? GameSession::NeverStarted : GameSession::Starting;
}

} // namespace solero
