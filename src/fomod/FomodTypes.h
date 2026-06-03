#pragma once
#include <QString>
#include <QList>

namespace solero {

enum class GroupType { ExactlyOne, AtMostOne, AtLeastOne, All, Any };
enum class OptionType { Required, Optional, Recommended, NotUsable, CouldBeUsable };

struct FomodFile {
    QString source;
    QString destination;
    bool    isFolder = false;
    int     priority = 0;
};

struct FomodFlag { QString name; QString value; };

struct FomodOption {
    QString name;
    QString description;
    QString imagePath;
    OptionType baseType = OptionType::Optional;
    QList<FomodFile> files;
    QList<FomodFlag> flags;
    QString conditionTypeXml;
};

struct FomodGroup {
    QString name;
    GroupType type = GroupType::Any;
    QList<FomodOption> options;
};

struct FomodStep {
    QString name;
    QString visibleConditionXml;
    QList<FomodGroup> groups;
};

struct FomodModule {
    QString moduleName;
    QList<FomodFile> requiredFiles;
    QList<FomodStep> steps;
    QString conditionalInstallsXml;
    bool valid = false;
};

} // namespace solero
