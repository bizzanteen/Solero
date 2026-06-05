#pragma once
#include <QString>

namespace solero {

// curl-based wrapper around the Nexus Mods v1 API. Reuses ToolDownloader's
// ~/.nexus_api_key reader for the apikey header. All methods are static and
// block (run curl via QProcess); callers should pump the UI as needed.
class NexusApi {
public:
    static constexpr const char* kDefaultGame = "skyrimspecialedition";

    struct ModInfo { bool ok = false; QString version, name, endorseStatus; };
    static ModInfo modInfo(const QString& modId, const QString& game = kDefaultGame);

    static QString latestVersion(const QString& modId, const QString& game = kDefaultGame);

    struct EndorseResult { bool ok = false; QString message; };
    static EndorseResult endorse(const QString& modId, const QString& version, bool abstain = false,
                                 const QString& game = kDefaultGame);

    struct Md5Match { bool ok = false; QString modId, fileId, version, modName; };
    static Md5Match md5Search(const QString& md5, const QString& game = kDefaultGame);

    static QString fileVersion(const QString& modId, const QString& fileId,
                               const QString& game = kDefaultGame);

    static bool keyAvailable();

    // Nexus account sign-in. The key is stored in ~/.nexus_api_key, which
    // ToolDownloader/NexusApi/NxmHandler all read for the apikey header.
    struct UserInfo { bool ok = false; QString name; bool premium = false; };
    static UserInfo validateUser(const QString& key = QString()); // GET /v1/users/validate.json
    static bool setApiKey(const QString& key);   // write ~/.nexus_api_key (trimmed) atomically
    static void clearApiKey();                    // remove ~/.nexus_api_key
    static QString apiKeyPath();                  // ~/.nexus_api_key
};

} // namespace solero
