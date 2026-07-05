#pragma once
// Solero log/text redaction - strip personally-identifying home paths before a
// report leaves the machine. Pure, no Qt-GUI dependency, unit-tested.
#include <QString>

namespace solero {

// Replace the user's home directory with "~" in an arbitrary string. Handles the
// canonical QDir::homePath() form plus the "/home/<user>" and "/var/home/<user>"
// spellings (on Bazzite /home is a symlink to /var/home, so a raw log can contain
// either). Leaves all other text intact.
QString redact(QString s);

} // namespace solero
