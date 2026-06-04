#include "PluginScanner.h"
#include "core/ModList.h"
#include "core/Types.h"
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
        QString data = childCI(stagingRoot + "/" + e.id, "Data");
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

    // Base-game masters take a fixed canonical order at the very top.
    static const QStringList kBaseOrder = {
        "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"
    };
    QStringList baseMasters, otherMasters;
    for (const QString& m : masters) {
        bool isBase = false;
        for (const QString& b : kBaseOrder)
            if (m.compare(b, Qt::CaseInsensitive) == 0) { isBase = true; break; }
        if (!isBase) otherMasters << m;
    }
    QStringList orderedBase;
    for (const QString& b : kBaseOrder)
        for (const QString& m : masters)
            if (m.compare(b, Qt::CaseInsensitive) == 0) { orderedBase << m; break; }

    otherMasters.sort(Qt::CaseInsensitive);
    lights.sort(Qt::CaseInsensitive);
    esps.sort(Qt::CaseInsensitive);
    return orderedBase + otherMasters + lights + esps;
}
}
