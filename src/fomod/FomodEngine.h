#pragma once
#include "FomodTypes.h"
#include "FomodCondition.h"
#include <QString>
#include <QHash>
#include <functional>

namespace solero {

class FomodEngine {
public:
    using Selection = QHash<QString, bool>;
    using FilePresent = std::function<bool(const QString&)>;

    bool load(const QString& moduleConfigPath);
    const FomodModule& module() const { return m_module; }
    QHash<QString, QString> flagsFor(const Selection& sel) const;
    bool isStepVisible(int stepIndex, const Selection& sel, const FilePresent& present) const;
    QList<FomodFile> collectFiles(const Selection& sel,
                                  const FilePresent& present = [](const QString&){ return false; }) const;
    static QString selKey(int step, int group, int opt);

private:
    FomodModule m_module;
};

} // namespace solero
