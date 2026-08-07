#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include <QMap>

namespace solero {

// Mod version variant (Keep Both)
// One installed version of a Nexus mod (Keep Both). The owning ModEntry's
// version/nexusFileId/stagingFolder/sourceArchive/hasFomodChoices fields always
// mirror variants[activeVariant]; ModList::setActiveVariant is the only writer.
struct ModVariant {
    QString version;
    QString nexusFileId;
    QString stagingFolder;
    QString sourceArchive;
    bool    hasFomodChoices = false;
};

// Mod list entry

enum class EntryType { Mod, Separator };
enum class RuntimeType { Native, Proton };

struct ModEntry {
    EntryType type = EntryType::Mod;

    // Common
    QString id;          // stable UUID, generated on install - internal key
    // On-disk staging folder name (sanitized, unique mod name). The id remains
    // the stable internal key; this decouples the folder name from it so the
    // staging dir is human-readable. Empty on older saves -> backfilled by
    // Profile::migrateStagingFolders(). Resolve paths via stagingPathFor().
    QString stagingFolder;

    // Mod fields
    QString name;
    QString version;
    QString nexusModId;  // empty if not from Nexus
    QString nexusFileId; // empty if not from Nexus
    QString parentId;    // parent mod's entry id (multi-file group child)
    bool enabled = true;
    bool hasFomodChoices = false;
    bool isOutputMod = false;
    // LEGACY / migration-only: old profiles stored the managed shader cache as a
    // hidden mod-list entry with this flag. It's now first-class Profile state
    // (ManagedShaderCache / Profile::shaderCache). Profile::load() still READS this
    // to lift such an entry out of the list; it is never written back.
    bool isManagedCache = false;
    // isFomod: the source archive carries a fomod/ModuleConfig.xml. fomodStatus is
    // how the choices were recovered: "" unknown, "reconstructed" (by file-diff),
    // "needs-rerun" (flag-driven, not reconstructable), or "manual".
    bool isFomod = false;
    QString fomodStatus;
    QStringList tags;
    QString sourceArchive; // archive path this mod was installed from (for Reinstall)
    QString note;          // free-form user note (shown/edited in the Mod Info panel)
    QList<ModVariant> variants; // empty for single-version mods
    int activeVariant = -1;     // index into variants; -1 iff variants is empty

    // Separator fields
    QString color;       // hex e.g. "#c0392b"
    QString icon;        // icon resource path e.g. ":/icons/separators/combat.svg", or empty
    bool collapsed = false;
    // Nesting depth for Separator entries only (meaningless for Mods): 0 = top-level
    // category, 1 = sub-category, etc. A separator nests under the nearest preceding
    // separator of a shallower level. (Distinct from parentId, which groups mods.)
    int separatorLevel = 0;
};

// Managed Community Shaders shader cache
// First-class per-profile state (not a mod-list entry): the captured shaders live
// at <stagingDir>/<folderFor(activeKey)>/Data/ShaderCache/, are deployed last so
// Plugin list entry

struct PluginEntry {
    QString filename;    // e.g. "SkyUI.esp"
    bool enabled = true;
    bool isMaster = false;   // .esm
    bool isLight = false;    // .esl
    QStringList masters;     // master files this plugin depends on (TES4 MAST)
    bool isOfficial = false; // base game / Creation Club content - locked
};

// Executable / tool

struct ToolAction {
    QString label;        // e.g. "Run TexGen"
    QString binaryPath;   // absolute path to the secondary exe/binary
    QString arguments;
    QString outputModId;  // optional capture target for this action
};

struct Executable {
    QString id;
    QString name;
    QString binaryPath;
    QString workingDir;
    QString arguments;
    RuntimeType runtime = RuntimeType::Native;
    QString protonVersion;
    QString winePrefix;
    bool runThroughDeployer = false;
    bool isPrimary = false;
    // Tool-mode only
    bool isCapturingOutput = false;
    QString outputModId;     // if isCapturingOutput; empty = Overwrite
    // Extra capture roots (relative to gameDir, or absolute) walked alongside Data
    // when isCapturingOutput is set - e.g. "DynDOLOD_Output" for DynDOLOD, or an
    // xEdit root log dir. Empty = capture Data only (unchanged behavior).
    QStringList captureRoots;
    // True for tools that write to their own output path (e.g. Radium) - Solero
    // skips the Data capture walk and relies on the tool's configured output dir.
    bool writesOutputDirectly = false;
    // True for the game launch itself: its runtime writes (Community Shaders' cache,
    // CS settings, ImGui ini) are meant to stay in the live Data dir (no-VFS design)
    // and, for the shader cache, be handled by the dedicated managed-cache capture -
    // NOT swept into Overwrite by the custom-tool loose capture.
    bool suppressLooseCapture = false;
    QString iconPath;
    QList<ToolAction> extraActions;
};

// AI transaction

struct FileSnapshot {
    QString filePath;
    QByteArray before;
    QByteArray after;
};

struct AITransaction {
    QString id;              // UUID
    QDateTime timestamp;
    QString description;
    QList<FileSnapshot> snapshots;
    bool reverted = false;
};

} // namespace solero
