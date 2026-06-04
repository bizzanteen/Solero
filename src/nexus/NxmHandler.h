#pragma once
#include <QString>
namespace solero {
struct NxmLink {
    QString game, modId, fileId, key, expires, userId;
    bool valid = false;
};
class NxmHandler {
public:
    static NxmLink parse(const QString& url);
    // Resolve the CDN download URL via the Nexus API (uses key/expires from the
    // link when present so free accounts can download mod-manager links).
    static QString resolveDownloadUrl(const NxmLink& link);
    // The mod file's display name (for the saved filename); "" on failure.
    static QString fileName(const NxmLink& link);
};
}
