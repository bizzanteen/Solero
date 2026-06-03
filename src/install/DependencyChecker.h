#pragma once
#include <QString>
#include <QStringList>
#include <QHash>
namespace solero { class ModList; }
namespace solero {
class DependencyChecker {
public:
    static QHash<QString, QStringList> check(const ModList& list, const QString& stagingRoot);
};
}
