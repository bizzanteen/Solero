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
    QString id;          // stable UUID, generated on install

    // Mod fields
    QString name;
    QString version;
    QString nexusModId;  // empty if not from Nexus
    QString nexusFileId; // empty if not from Nexus
    QString parentId;    // parent mod's entry id (multi-file group child)
    bool enabled = true;
    bool hasFomodChoices = false;
    bool isOutputMod = false;
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
