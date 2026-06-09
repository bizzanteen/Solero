#pragma once
#include <QString>
#include <QList>

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

    // Browsing: search (v2 GraphQL) + curated lists / details / files (v1)
    struct ModSummary { QString modId, name, summary, author, pictureUrl; int endorsements = 0; bool adult = false; };
    static QList<ModSummary> search(const QString& query, int count = 25, const QString& game = kDefaultGame);
    static QList<ModSummary> trending(const QString& game = kDefaultGame);
    static QList<ModSummary> latestAdded(const QString& game = kDefaultGame);
    static QList<ModSummary> latestUpdated(const QString& game = kDefaultGame);

    struct ModDetails { bool ok = false; QString modId, name, summary, description, pictureUrl, author, version, endorseStatus; int endorsements = 0; bool adult = false; };
    static ModDetails modDetails(const QString& modId, const QString& game = kDefaultGame);

    struct NexusFile { QString fileId, name, version, category, description; qint64 sizeKb = 0; };
    static QList<NexusFile> files(const QString& modId, const QString& game = kDefaultGame);

    // Lists a mod's Nexus mod requirements (v2 GraphQL). external == true marks
    // an off-site requirement that isn't a Nexus mod on this game. Returns empty
    // on any failure (network, errors, not-found).
    // external == true -> off-site (not a Nexus mod for this game); url points to the
    // off-site page and modId is a meaningless "0". For Nexus reqs, url is empty.
    struct ModRequirement { QString modId, modName, notes, url; bool external = false; };
    static QList<ModRequirement> modRequirements(const QString& modId, const QString& game = kDefaultGame);

    // First mirror URI from v1 download_link.json. Premium accounts get this
    // without an nxm key/expires; non-premium returns "" (caller handles).
    static QString downloadUrl(const QString& modId, const QString& fileId, const QString& game = kDefaultGame);

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
