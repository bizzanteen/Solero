#include "PluginScanner.h"
#include "core/ModList.h"
#include "core/Types.h"
#include <QDir>
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

QStringList PluginScanner::scanGameData(const QString& gameDir) {
    QDir d(gameDir + "/Data");
    QStringList masters, others;
    if (d.exists()) {
        const auto plugins = d.entryList({"*.esp","*.esm","*.esl","*.ESP","*.ESM","*.ESL"},
                                         QDir::Files, QDir::Name);
        for (const QString& p : plugins) {
            if (p.endsWith(".esm", Qt::CaseInsensitive)) masters << p;
            else others << p;
        }
    }
    masters.sort(Qt::CaseInsensitive);
    others.sort(Qt::CaseInsensitive);
    return masters + others;
}
}
