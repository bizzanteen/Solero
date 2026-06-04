#pragma once
#include "core/Types.h"
#include <QString>
namespace solero {
class ToolRunner {
public:
    struct Result { bool launched=false; QString error; QString output; };
    // gameDir/stagingDir used for output capture. Blocks until the tool exits.
    static Result run(const Executable& exe, const QString& gameDir, const QString& stagingRoot);
private:
    static QStringList snapshotFiles(const QString& dir);
};
}
