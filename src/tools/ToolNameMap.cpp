#include "ToolNameMap.h"
#include <QFileInfo>
namespace solero {

QString presetIdForToolName(const QString& name, const QString& binary) {
    const QString base = QFileInfo(binary).fileName(); // exe basename
    // Test a keyword against the title OR the binary basename (case-insensitive).
    auto hits = [&](const char* kw) {
        return name.contains(QLatin1String(kw), Qt::CaseInsensitive)
            || base.contains(QLatin1String(kw), Qt::CaseInsensitive);
    };

    // Order matters: check the more specific names first (e.g. "TexGen" and
    // "DynDOLOD" both map to the dyndolod preset; SSEEdit/xEdit -> xedit).
    if (hits("DynDOLOD") || hits("TexGen"))     return "dyndolod";
    if (hits("SSEEdit") || hits("xEdit"))       return "xedit";
    if (hits("Nemesis"))                        return "nemesis";
    if (hits("Pandora"))                        return "pandora";
    if (hits("Synthesis"))                      return "synthesis";
    if (hits("ESLifier"))                       return "eslifier";
    if (hits("PGPatcher"))                      return "pgpatcher";
    if (hits("Radium"))                         return "radium";
    return QString();
}

QString findOutputModId(const QList<ModEntry>& mods, const QString& toolName) {
    const QString wanted = toolName.trimmed() + " Output";
    // Pass 1: exact "<toolName> Output" name.
    for (const ModEntry& m : mods) {
        if (m.type != EntryType::Mod) continue;
        if (m.name.compare(wanted, Qt::CaseInsensitive) == 0) return m.id;
    }
    // Pass 2: fuzzy - shares the tool keyword and looks like an output target.
    const QString key = toolName.trimmed();
    if (key.isEmpty()) return QString();
    for (const ModEntry& m : mods) {
        if (m.type != EntryType::Mod) continue;
        if (!m.name.contains(key, Qt::CaseInsensitive)) continue;
        if (m.name.contains("Resource", Qt::CaseInsensitive)) continue;
        if (m.isOutputMod || m.name.contains("Output", Qt::CaseInsensitive))
            return m.id;
    }
    return QString();
}

}
