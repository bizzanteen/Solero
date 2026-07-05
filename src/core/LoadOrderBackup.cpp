#include "LoadOrderBackup.h"
#include "Profile.h"
#include "PluginList.h"
#include "ModList.h"
#include "FileUtil.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>

namespace solero {
namespace LoadOrderBackup {

QString dir(const Profile& profile) {
    return profile.path() + "/lo-backups";
}

QString create(const Profile& profile, const QString& label) {
    const QDateTime now = QDateTime::currentDateTime();
    const QString effectiveLabel =
        label.trimmed().isEmpty() ? now.toString("yyyy-MM-dd HH:mm:ss") : label.trimmed();

    QJsonArray plugins;
    const PluginList& pl = profile.pluginList();
    for (int i = 0; i < pl.count(); ++i) {
        const PluginEntry& p = pl.at(i);
        QJsonObject o;
        o["name"]    = p.filename;
        o["enabled"] = p.enabled;
        plugins.append(o);
    }

    // Capture the ordered mod list too: id + enabled flag + separator marker, so a
    // bad install/reorder is reversible alongside the plugin order.
    QJsonArray mods;
    const ModList& ml = profile.modList();
    for (int i = 0; i < ml.count(); ++i) {
        const ModEntry& e = ml.at(i);
        QJsonObject o;
        o["id"]        = e.id;
        o["enabled"]   = e.enabled;
        o["separator"] = (e.type == EntryType::Separator);
        mods.append(o);
    }

    QJsonObject root;
    root["label"]   = effectiveLabel;
    root["created"] = now.toString(Qt::ISODate);
    root["plugins"] = plugins;
    root["mods"]    = mods;

    const QString d = dir(profile);
    QDir().mkpath(d);
    // Timestamped filename; append a counter if the second collides.
    const QString base = "lo-" + now.toString("yyyyMMdd-HHmmss");
    QString path = d + "/" + base + ".json";
    for (int n = 1; QFile::exists(path); ++n)
        path = d + "/" + base + "-" + QString::number(n) + ".json";

    if (!atomicWrite(path, QJsonDocument(root).toJson(QJsonDocument::Indented)))
        return {};
    return path;
}

QList<LoadOrderBackupInfo> list(const Profile& profile) {
    QList<LoadOrderBackupInfo> out;
    QDir d(dir(profile));
    if (!d.exists()) return out; // no backups dir -> nothing to restore

    for (const QString& fn : d.entryList({"*.json"}, QDir::Files)) {
        const QString path = d.filePath(fn);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        LoadOrderBackupInfo info;
        info.path        = path;
        info.created     = QDateTime::fromString(root["created"].toString(), Qt::ISODate);
        info.label       = root["label"].toString();
        info.pluginCount = root["plugins"].toArray().size();
        // -1 flags a legacy plugin-only backup (no "mods" section); a present but
        // empty array is a genuine zero-mod snapshot (0).
        info.modCount    = root.contains("mods") ? root["mods"].toArray().size() : -1;
        if (info.label.isEmpty())
            info.label = info.created.isValid()
                ? info.created.toString("yyyy-MM-dd HH:mm:ss") : fn;
        out.append(info);
    }
    // Newest first; fall back to filename when timestamps are equal/invalid.
    std::sort(out.begin(), out.end(), [](const LoadOrderBackupInfo& a,
                                         const LoadOrderBackupInfo& b) {
        if (a.created != b.created) return a.created > b.created;
        return a.path > b.path;
    });
    return out;
}

LoadOrderSnapshot load(const QString& path) {
    LoadOrderSnapshot snap;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return snap;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    snap.label   = root["label"].toString();
    snap.created = QDateTime::fromString(root["created"].toString(), Qt::ISODate);
    for (const auto& v : root["plugins"].toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o["name"].toString();
        if (name.isEmpty()) continue;
        snap.plugins.append({name, o["enabled"].toBool(false)});
    }
    // Mod list: only present in newer backups. Absent means hasMods stays
    // false and callers leave the live mod list untouched (plugin-only restore).
    if (root.contains("mods")) {
        snap.hasMods = true;
        for (const auto& v : root["mods"].toArray()) {
            const QJsonObject o = v.toObject();
            const QString id = o["id"].toString();
            if (id.isEmpty()) continue;
            ModListSnapshotEntry e;
            e.id          = id;
            e.enabled     = o["enabled"].toBool(false);
            e.isSeparator = o["separator"].toBool(false);
            snap.mods.append(e);
        }
    }
    snap.valid = true;
    return snap;
}

} // namespace LoadOrderBackup
} // namespace solero
