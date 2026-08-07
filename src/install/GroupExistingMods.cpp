#include "GroupExistingMods.h"
#include "core/Log.h"
#include <QHash>
#include <QStringList>

namespace solero {

int groupExistingMultiFileMods(ModList& list) {
    // nexusModId -> entry ids, in list order.
    QHash<QString, QStringList> byModId;
    QStringList order;
    for (int i = 0; i < list.count(); ++i) {
        const ModEntry& e = list.at(i);
        if (e.type != EntryType::Mod || e.nexusModId.isEmpty()) continue;
        if (!e.parentId.isEmpty()) { byModId.remove(e.nexusModId); continue; } // already grouped
        if (!byModId.contains(e.nexusModId)) order << e.nexusModId;
        byModId[e.nexusModId] << e.id;
    }

    int moved = 0;
    for (const QString& modId : order) {
        const QStringList ids = byModId.value(modId);
        if (ids.size() < 2) continue;
        const QString head = ids.first();
        for (int i = 1; i < ids.size(); ++i)
            if (list.groupUnder(ids.at(i), head)) ++moved;
    }
    if (moved > 0) qCInfo(lcApp) << "grouped" << moved << "existing multi-file mod entries";
    return moved;
}

} // namespace solero
