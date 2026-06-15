#include "MirrorPick.h"
#include <QJsonArray>
#include <QJsonObject>

namespace solero {

QString pickMirror(const QJsonArray& mirrors, const QString& preferred) {
    if (mirrors.isEmpty()) return {};
    if (!preferred.isEmpty()) {
        for (const auto& v : mirrors) {
            const QJsonObject o = v.toObject();
            if (o["short_name"].toString().compare(preferred, Qt::CaseInsensitive) == 0)
                return o["URI"].toString();
        }
    }
    return mirrors.first().toObject()["URI"].toString();
}

QStringList mirrorServerNames(const QJsonArray& mirrors) {
    QStringList out;
    for (const auto& v : mirrors) {
        const QString s = v.toObject()["short_name"].toString();
        if (!s.isEmpty() && !out.contains(s)) out.append(s);
    }
    return out;
}

} // namespace solero
