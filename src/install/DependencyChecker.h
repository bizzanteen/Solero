#pragma once
#include <QString>
#include <QStringList>
#include <QHash>
namespace solero { class ModList; }
namespace solero {
class DependencyChecker {
public:
    static QHash<QString, QStringList> check(const ModList& list, const QString& stagingRoot);
    // Drop a mod's cached staging scan; call when its staged files change. check()
    // also auto-invalidates on the staging dir's mtime. No-arg form clears all.
    static void invalidate(const QString& modId);
    static void invalidate();
};
}
