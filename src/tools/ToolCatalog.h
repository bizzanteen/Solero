#pragma once
#include <QString>
#include <QStringList>
#include <QList>
namespace solero {
enum class ToolSource { Nexus, Github };
struct ToolPreset {
    QString id, name, author, creditUrl;
    ToolSource source = ToolSource::Nexus;
    // Nexus
    QString nexusGame = "skyrimspecialedition";
    QString nexusModId;       // e.g. "164"
    // Github
    QString githubOwner, githubRepo;
    QString assetMatch;       // substring to pick the release asset (e.g. "linux", ".7z")
    // After extract:
    QString exeRelPath;       // e.g. "SSEEdit.exe"
    QString args;
    bool proton = true;
    QStringList needs;        // other preset ids this depends on
    QString iconResource;     // ":/icons/tools/<id>.png"
    bool producesOutput = false;
    QString outputModName;    // e.g. "DynDOLOD Output"
    struct PresetAction { QString label, exeRelPath, args, outputModName; };
    QList<PresetAction> extraActions;
};
class ToolCatalog {
public:
    static const QList<ToolPreset>& presets();
    static const ToolPreset* byId(const QString& id);
    static QString dyndolodResourcesModId();
};
}
