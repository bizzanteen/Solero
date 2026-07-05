#include "report/Redactor.h"

#include <QDir>
#include <QFileInfo>

namespace solero {

QString redact(QString s) {
    const QString home = QDir::homePath();
    // The user component of the home dir (e.g. "eamon"), used to build the
    // /var/home/<user> and /home/<user> spellings that a symlinked home can produce.
    const QString user = QFileInfo(home).fileName();

    // Order matters: "/var/home/<user>" contains the substring "/home/<user>", so
    // the longer /var/home form must be replaced first or it would leave "/var~".
    if (!user.isEmpty()) {
        s.replace("/var/home/" + user, QStringLiteral("~"));
        s.replace("/home/" + user, QStringLiteral("~"));
    }
    // Finally the canonical home path (covers any spelling QDir reports, e.g. a
    // non-standard $HOME) - done last so it can't undercut the explicit forms above.
    if (!home.isEmpty())
        s.replace(home, QStringLiteral("~"));
    return s;
}

} // namespace solero
