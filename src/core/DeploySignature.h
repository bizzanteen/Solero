#pragma once
#include "core/Profile.h"
#include "core/Types.h"
#include <QByteArray>
#include <QStringList>

namespace solero {

// Fingerprint of what a deploy would link: the ordered enabled mods with their
// active variants, the per-file hide rules and the forced winners. Equal
// signatures deploy the same files, so the UI can go back to "Deployed" after a
// net-zero change.
//
// Not included: INI/save redirection and staged file content. Those change the
// deployment without changing this, so callers track them separately.
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
