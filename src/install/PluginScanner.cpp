#include "PluginScanner.h"
#include "core/ModList.h"
#include "core/Types.h"
#include "core/StagingFolder.h"
#include <QDir>
#include <QFile>
namespace solero {
static QString childCI(const QString& parent, const QString& name) {
    QDir d(parent);
    if (!d.exists()) return {};
    for (const QString& e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
        if (e.compare(name, Qt::CaseInsensitive) == 0) return parent + "/" + e;
    return {};
}
QStringList PluginScanner::scan(const ModList& list, const QString& stagingRoot) {
    QStringList out;
    for (const auto& e : list) {
        if (e.type != EntryType::Mod || !e.enabled) continue;
        QString data = childCI(stagingPathFor(stagingRoot, e), "Data");
        if (data.isEmpty()) continue;
        QDir d(data);
        const auto plugins = d.entryList({"*.esp","*.esm","*.esl","*.ESP","*.ESM","*.ESL"},
                                         QDir::Files, QDir::Name);
        for (const QString& p : plugins) if (!out.contains(p, Qt::CaseInsensitive)) out << p;
    }
    return out;
}

PluginFlags PluginScanner::readFlags(const QString& pluginPath) {
    PluginFlags pf;
    QFile f(pluginPath);
    if (!f.open(QIODevice::ReadOnly)) return pf;
    const QByteArray head = f.read(12);
    if (head.size() < 12) return pf;
    if (head[0] != 'T' || head[1] != 'E' || head[2] != 'S' || head[3] != '4')
        return pf;
    // Record flags: little-endian uint32 at byte offset 8.
    const quint32 flags =
          (static_cast<quint8>(head[8]))
        | (static_cast<quint8>(head[9])  << 8)
        | (static_cast<quint8>(head[10]) << 16)
        | (static_cast<quint8>(head[11]) << 24);
    pf.isMaster = (flags & 0x00000001u) != 0;
    pf.isLight  = (flags & 0x00000200u) != 0; // ESL flag
    pf.ok = true;
    return pf;
}

QStringList PluginScanner::readMasters(const QString& pluginPath) {
    QStringList out;
    QFile f(pluginPath);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray head = f.read(24); // TES4 record header
    if (head.size() < 24) return out;
    if (head[0] != 'T' || head[1] != 'E' || head[2] != 'S' || head[3] != '4')
        return out;
    auto u32 = [](const QByteArray& b, int o) -> quint32 {
        return (static_cast<quint8>(b[o]))
             | (static_cast<quint8>(b[o + 1]) << 8)
             | (static_cast<quint8>(b[o + 2]) << 16)
             | (static_cast<quint8>(b[o + 3]) << 24);
    };
    const quint32 dataSize = u32(head, 4); // size of subrecord block
    const QByteArray body = f.read(dataSize);
    if (static_cast<quint32>(body.size()) < dataSize) return out;

    int pos = 0;
    const int n = body.size();
    while (pos + 6 <= n) { // 4-byte type + 2-byte size
        const char* t = body.constData() + pos;
        const quint16 size =
              static_cast<quint8>(body[pos + 4])
            | (static_cast<quint8>(body[pos + 5]) << 8);
        pos += 6;
        if (pos + size > n) break; // bounds check - truncated/corrupt
        if (t[0] == 'M' && t[1] == 'A' && t[2] == 'S' && t[3] == 'T') {
            QByteArray name = body.mid(pos, size);
            int z = name.indexOf('\0');
            if (z >= 0) name.truncate(z);
            if (!name.isEmpty()) out << QString::fromLatin1(name);
        }
        pos += size;
    }
    return out;
}

QStringList PluginScanner::officialPlugins(const QString& gameDir) {
    static const QStringList kBase = {
        "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"
    };
    QStringList out = kBase;
    QFile ccc(gameDir + "/Skyrim.ccc");
    if (ccc.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!ccc.atEnd()) {
            const QString line = QString::fromUtf8(ccc.readLine()).trimmed();
            if (!line.isEmpty()) out << line;
        }
    }
    return out;
}

QStringList PluginScanner::scanGameData(const QString& gameDir) {
    const QString dataPath = gameDir + "/Data";
    QDir d(dataPath);
    QStringList masters, lights, esps;
    if (d.exists()) {
        const auto plugins = d.entryList({"*.esp","*.esm","*.esl","*.ESP","*.ESM","*.ESL"},
                                         QDir::Files, QDir::Name);
        for (const QString& p : plugins) {
            PluginFlags pf = readFlags(dataPath + "/" + p);
            bool isMaster, isLight;
            if (pf.ok) {
                isMaster = pf.isMaster;
                isLight  = pf.isLight;
            } else {
                isMaster = p.endsWith(".esm", Qt::CaseInsensitive);
                isLight  = p.endsWith(".esl", Qt::CaseInsensitive);
            }
            // Light masters are bucketed separately even if they also carry the
            // master flag (ESL-flagged .esp / .esl files).
            if (isLight) lights << p;
            else if (isMaster) masters << p;
            else esps << p;
        }
    }

    // Official plugins (base masters + Skyrim.ccc) take a fixed canonical order
    // at the very top, only those actually present in Data.
    const QStringList official = officialPlugins(gameDir);
    auto isOfficial = [&](const QString& fn) {
        for (const QString& o : official)
            if (fn.compare(o, Qt::CaseInsensitive) == 0) return true;
        return false;
    };

    // Drop officials from the per-band buckets - they are emitted first.
    auto stripOfficials = [&](QStringList& bucket) {
        QStringList kept;
        for (const QString& p : bucket) if (!isOfficial(p)) kept << p;
        bucket = kept;
    };
    stripOfficials(masters);
    stripOfficials(lights);
    stripOfficials(esps);

    // Officials in official-list order, only those present in Data.
    QStringList orderedOfficial;
    {
        QDir dd(dataPath);
        const auto allFiles = dd.entryList({"*.esp","*.esm","*.esl","*.ESP","*.ESM","*.ESL"},
                                           QDir::Files, QDir::Name);
        for (const QString& o : official)
            for (const QString& f : allFiles)
                if (f.compare(o, Qt::CaseInsensitive) == 0) { orderedOfficial << f; break; }
    }

    masters.sort(Qt::CaseInsensitive);
    lights.sort(Qt::CaseInsensitive);
    esps.sort(Qt::CaseInsensitive);
    return orderedOfficial + masters + lights + esps;
}
}
