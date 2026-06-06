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
    // Inject an already-parsed module (e.g. for the Patch Wizard, which re-uses
    // effectiveType()/isStepVisible() against an in-memory FomodModule).
    void setModule(const FomodModule& m) { m_module = m; }
    QHash<QString, QString> flagsFor(const Selection& sel) const;

    // Set the predicate used to evaluate fileDependency conditions (by plugin
    // file name). If null, file-present checks default to false.
    void setFilePresent(FilePresent fn) { m_filePresent = std::move(fn); }
    bool filePresent(const QString& file) const {
        return m_filePresent ? m_filePresent(file) : false;
    }

    bool isStepVisible(int stepIndex, const Selection& sel) const;
    QList<FomodFile> collectFiles(const Selection& sel) const;

    // Evaluate an option's effective type, taking its conditional
    // (dependencyType) descriptor into account against current flags/files.
    OptionType effectiveType(const FomodOption& opt, const Selection& sel) const;

    static QString selKey(int step, int group, int opt);

private:
    FomodModule m_module;
    FilePresent m_filePresent;
};

} // namespace solero
