#pragma once
#include <QString>
#include <QList>
#include <QPair>
#include <QDateTime>

namespace solero {

class Profile;

// A load-order snapshot stored under <profile>/lo-backups/. Each backup is a
// JSON file capturing the full plugin load order (filename + enabled) at the
// time it was taken, so a risky LOOT sort / manual reorder can be reversed.

// Metadata about a backup on disk (cheap to enumerate, no full plugin list).
struct LoadOrderBackupInfo {
    QString   path;        // absolute path to the JSON file
    QString   label;       // user label (defaults to the timestamp)
    QDateTime created;     // creation time
    int       pluginCount = 0;
};

// A loaded snapshot: the ordered {filename, enabled} pairs plus metadata.
struct LoadOrderSnapshot {
    bool      valid = false;
    QString   label;
    QDateTime created;
    QList<QPair<QString, bool>> plugins; // in load order
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
