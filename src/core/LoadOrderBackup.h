#pragma once
#include <QString>
#include <QList>
#include <QPair>
#include <QDateTime>

namespace solero {

class Profile;

// A load-order snapshot stored under <profile>/lo-backups/. Each backup is a
// JSON file capturing the full plugin load order (filename + enabled) and the
// ordered mod list (id + enabled + separator flag) at the time it was taken, so
// a risky LOOT sort / manual reorder / bad install can be reversed. Older
// plugin-only snapshots (no "mods" section) still restore - their mod list is
// left untouched (see LoadOrderSnapshot::hasMods).

// Metadata about a backup on disk (cheap to enumerate, no full lists).
struct LoadOrderBackupInfo {
    QString   path;        // absolute path to the JSON file
    QString   label;       // user label (defaults to the timestamp)
    QDateTime created;     // creation time
    int       pluginCount = 0;
    int       modCount    = 0; // -1 when the backup predates the mod-list section
};

// One captured mod-list entry (in list order). Separators are captured too so
// the whole visual ordering is restored; `enabled` is meaningless for those.
struct ModListSnapshotEntry {
    QString id;
    bool    enabled     = false;
    bool    isSeparator = false;
};

// A loaded snapshot: the ordered {filename, enabled} plugin pairs plus, when the
// backup is new enough (hasMods), the ordered mod list. Both carry metadata.
struct LoadOrderSnapshot {
    bool      valid = false;
    QString   label;
    QDateTime created;
    QList<QPair<QString, bool>> plugins; // in load order
    bool      hasMods = false;           // false for old plugin-only backups
    QList<ModListSnapshotEntry> mods;    // in list order (empty unless hasMods)
};

namespace LoadOrderBackup {

// <profile>/lo-backups (not created until create() is called).
QString dir(const Profile& profile);

// Write a new timestamped snapshot of the profile's current plugin list.
// `label` is optional; empty -> the timestamp is used as the label.
// Returns the new file's path, or an empty string on failure.
QString create(const Profile& profile, const QString& label = QString());

// All backups for the profile, newest first. Missing dir -> empty list.
QList<LoadOrderBackupInfo> list(const Profile& profile);

// Read a snapshot file. snapshot.valid is false if it can't be read/parsed.
LoadOrderSnapshot load(const QString& path);

} // namespace LoadOrderBackup

} // namespace solero
