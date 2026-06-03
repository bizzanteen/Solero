#include "Mo2Importer.h"
#include <QStringList>
#include <QUuid>

namespace solero {

QList<ModEntry> Mo2Importer::parseModlist(const QString& modlistTxt) {
    QList<ModEntry> topToBottom; // as read (top = highest priority)
    for (const QString& raw : modlistTxt.split('\n')) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        QChar prefix = line.at(0);
        QString name = line.mid(1);
        if (prefix == '*') continue; // unmanaged/MO2-internal
        if (prefix != '+' && prefix != '-') continue;

        ModEntry e;
        if (name.endsWith("_separator")) {
            e.type = EntryType::Separator;
            e.name = name.left(name.size() - QString("_separator").size());
            e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            e.color = "#555555";
            e.enabled = true;
        } else {
            e.type = EntryType::Mod;
            e.name = name;
            e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            e.enabled = (prefix == '+');
        }
        topToBottom.append(e);
    }
    // Reverse into Solero order (index 0 = lowest priority).
    QList<ModEntry> out;
    for (int i = topToBottom.size() - 1; i >= 0; --i) out.append(topToBottom[i]);
    return out;
}

} // namespace solero
