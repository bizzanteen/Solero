#include "DataDirDetector.h"
#include <QSet>
#include <QFileInfo>

namespace solero {

static const QSet<QString> kDataDirs = {
    "meshes","textures","scripts","sound","music","interface","materials",
    // "shaders" is Community Shaders' Data/Shaders/Features/ home; without it,
    // a Shaders/-rooted archive gets stripped as a wrapper and installed to the
    // game root, so CS never finds the feature.
    "shaders","shadersfx","skse","seq","grass","lodsettings","dialogueviews","source",
    "calientetools","dyndolod","strings","facegen","video","misc",
    "actors","effects","clutter","architecture"
};
static const QSet<QString> kLoaders = { "skse64_loader.exe", "skse_loader.exe", "f4se_loader.exe" };

bool DataDirDetector::isDataMarkerDir(const QString& name) {
    return kDataDirs.contains(name.toLower());
}

bool DataDirDetector::isPluginFile(const QString& name) {
    QString l = name.toLower();
    return l.endsWith(".esp") || l.endsWith(".esm") || l.endsWith(".esl");
}

QString DataDirDetector::topComponent(const QString& path) {
    int i = path.indexOf('/');
    return i < 0 ? path : path.left(i);
}

InstallLayout DataDirDetector::detect(const QStringList& rawPaths) {
    InstallLayout layout;

    // Normalize: backslashes->slashes, drop leading "./" and "/".
    QStringList paths;
    for (QString p : rawPaths) {
        p.replace('\\', '/');
        while (p.startsWith("./")) p = p.mid(2);
        while (p.startsWith('/')) p = p.mid(1);
        if (!p.isEmpty()) paths.append(p);
    }
    if (paths.isEmpty()) return layout;

    // 1. FOMOD detection: find <root>/fomod/moduleconfig.xml
    for (const auto& p : paths) {
        QString lower = p.toLower();
        if (lower == "fomod/moduleconfig.xml") {
            layout.isFomod = true; layout.fomodRootLevel = 0; break;
        }
        int idx = lower.indexOf("/fomod/moduleconfig.xml");
        if (idx > 0) {
            QString prefix = p.left(idx);
            layout.isFomod = true;
            layout.fomodRootLevel = prefix.count('/') + 1;
            break;
        }
    }

    // 2. Strip common wrapper directories (single shared top dir, not a marker).
    int strip = 0;
    QStringList work = paths;
    while (true) {
        QString top; bool allShare = true;
        for (const auto& p : work) {
            if (!p.contains('/')) { allShare = false; break; }
            QString t = topComponent(p);
            if (top.isEmpty()) top = t;
            else if (t.compare(top, Qt::CaseInsensitive) != 0) { allShare = false; break; }
        }
        if (!allShare || top.isEmpty()) break;
        if (isDataMarkerDir(top) || top.compare("Data", Qt::CaseInsensitive) == 0) break;
        QStringList next;
        for (const auto& p : work) next.append(p.mid(top.length() + 1));
        work = next;
        ++strip;
    }
    layout.stripComponents = strip;

    // 3. Game-root vs Data-relative based on stripped top-level entries.
    bool hasDataDir = false, hasLoader = false, hasMarker = false;
    for (const auto& p : work) {
        QString t = topComponent(p);
        if (t.compare("Data", Qt::CaseInsensitive) == 0) hasDataDir = true;
        if (!p.contains('/')) {
            if (kLoaders.contains(t.toLower())) hasLoader = true;
            if (isPluginFile(t)) hasMarker = true;
        }
        if (isDataMarkerDir(t)) hasMarker = true;
    }

    if (hasDataDir || hasLoader) layout.wrapInData = false;
    else if (hasMarker)          layout.wrapInData = true;
    else                         layout.wrapInData = false;

    return layout;
}

} // namespace solero
