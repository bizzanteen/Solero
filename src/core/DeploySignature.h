#pragma once
#include "core/Profile.h"
#include "core/Types.h"
#include <QByteArray>
#include <QStringList>

namespace solero {

// A stable fingerprint of everything about a profile that determines WHAT a deploy
// links into the game dir: the ordered enabled mods (plus each mod's active version
// variant) and the per-file hide rules and forced conflict winners. Two profiles
// with equal signatures deploy the same set of files, so the UI can flip back to
// "Deployed" after a net-zero change (disable then re-enable a mod, reorder then
// reorder back, hide then unhide a file). The Community Shaders cache rides along
// inside the CS mod, so its enabled/disabled state is already covered by the mod list.
//
// Deliberately NOT included: INI/save redirection and staged file CONTENT (a data
// rename, reinstall, patch run, ini toggle). Those change the deployment without
// changing this signature, so callers track them as sticky "content" dirtiness.
inline QByteArray deploySignature(const Profile& p) {
    QByteArray s;
    for (const auto& e : p.modList()) {
        if (e.type != EntryType::Mod || !e.enabled) continue;
        s += e.id.toUtf8();
        s += ':';
        s += QByteArray::number(e.activeVariant);
        s += ';';
    }
    {
        const auto& h = p.hiddenFiles();
        QStringList keys = h.keys();
        keys.sort();
        for (const auto& k : keys) {
            QStringList v = h.value(k).values();
            v.sort();
            s += "H:"; s += k.toUtf8(); s += '='; s += v.join(',').toUtf8(); s += ';';
        }
    }
    {
        const auto& o = p.fileOverrides();
        QStringList keys = o.keys();
        keys.sort();
        for (const auto& k : keys) {
            s += "O:"; s += k.toUtf8(); s += '='; s += o.value(k).toUtf8(); s += ';';
        }
    }
    return s;
}

} // namespace solero
