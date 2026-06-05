#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// Metadata for a single Wabbajack modlist as reported by the
// jackify-engine `list-modlists -json` gallery output.
struct WabbajackModlist {
    QString title, description, author, machineUrl, namespacedName, game, gameHuman, version;
    QString imageUrl, readmeUrl, websiteUrl;
    QString downloadSizeStr, installSizeStr; // human-formatted, from the JSON *Formatted fields
    bool nsfw = false, official = false, utility = false;
    QStringList tags;
};

} // namespace solero
