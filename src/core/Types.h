#pragma once
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>

namespace solero {

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
    // The hidden, Solero-managed Community Shaders shader-cache mod. Captured after
    // play (Data/ShaderCache only), deployed last (last-wins), hidden from the list,
    // and cleared via the CS mod's right-click "Clear Shader Cache" action.
    bool isManagedCache = false;
    // FOMOD detection (back-filled by the load-order FOMOD scan). isFomod is true
    // when the mod's source archive carries a fomod/ModuleConfig.xml. fomodStatus
    // tracks how its choices were recovered: "" (unknown/none), "reconstructed"
    // (choices recovered by file-diff), "needs-rerun" (flag-driven; not
    // reconstructable - re-run the installer to record), or "manual".
    bool isFomod = false;
    QString fomodStatus;
    QStringList tags;
    QString sourceArchive; // archive path this mod was installed from (for Reinstall)
    QString note;          // free-form user note (shown/edited in the Mod Info panel)

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
// at <stagingDir>/<stagingFolder>/Data/ShaderCache/, are deployed last so they win
// all conflicts, and are invisible in the mod list. "Active" = managed && folder set.
struct ManagedShaderCache {
    bool managed = false;
    QString stagingFolder;
    bool active() const { return managed && !stagingFolder.isEmpty(); }
};

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
