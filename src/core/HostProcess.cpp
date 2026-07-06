#include "core/HostProcess.h"
#include <QFileInfo>

namespace solero {

bool runningInFlatpak() {
    static const bool v = QFileInfo::exists(QStringLiteral("/.flatpak-info"));
    return v;
}

HostCommand hostCommand(const QString& program, const QStringList& args,
                        const QMap<QString, QString>& envOverrides, bool inFlatpak) {
    if (!inFlatpak)
        return HostCommand{ program, args, true };

    QStringList out;
    out << QStringLiteral("--host");
    // QMap iterates in sorted key order -> deterministic argv.
    for (auto it = envOverrides.cbegin(); it != envOverrides.cend(); ++it)
        out << QStringLiteral("--env=%1=%2").arg(it.key(), it.value());
    out << program;
    out << args;
    return HostCommand{ QStringLiteral("flatpak-spawn"), out, false };
}

} // namespace solero
