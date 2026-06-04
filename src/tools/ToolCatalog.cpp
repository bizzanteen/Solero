#include "ToolCatalog.h"
namespace solero {
const QList<ToolPreset>& ToolCatalog::presets() {
    static const QList<ToolPreset> p = [] {
        QList<ToolPreset> v;
        auto nexus = [](QString id, QString name, QString author, QString modId,
                        QString exe, QString args, QStringList needs = {}) {
            ToolPreset t; t.id=id; t.name=name; t.author=author; t.source=ToolSource::Nexus;
            t.nexusModId=modId; t.creditUrl="https://www.nexusmods.com/skyrimspecialedition/mods/"+modId;
            t.exeRelPath=exe; t.args=args; t.proton=true; t.needs=needs; return t;
        };
        auto github = [](QString id, QString name, QString author, QString owner, QString repo,
                         QString assetMatch, QString exe, QString args) {
            ToolPreset t; t.id=id; t.name=name; t.author=author; t.source=ToolSource::Github;
            t.githubOwner=owner; t.githubRepo=repo; t.assetMatch=assetMatch;
            t.creditUrl="https://github.com/"+owner+"/"+repo;
            t.exeRelPath=exe; t.args=args; t.proton=true; return t;
        };

        v << nexus("xedit", "SSEEdit (xEdit)", "ElminsterAU & the xEdit Team", "164",
                   "SSEEdit.exe", "-sse");

        v << nexus("xedit-qac", "xEdit Quick Auto Clean", "ElminsterAU & the xEdit Team", "164",
                   "SSEEditQuickAutoClean.exe", "-sse -qac -autoexit", {"xedit"});

        {
            ToolPreset t = nexus("dyndolod", "DynDOLOD", "Sheson", "68518",
                                 "DynDOLODx64.exe", "-sse");
            t.producesOutput = true;
            t.outputModName = "DynDOLOD Output";
            t.extraActions = {{ "Run TexGen", "TexGenx64.exe", "-sse", "TexGen Output" }};
            v << t;
        }

        {
            ToolPreset t = nexus("nemesis", "Nemesis", "ShikyoKira", "60033",
                                 "Nemesis Unlimited Behavior Engine.exe", "");
            t.producesOutput = true;
            t.outputModName = "Nemesis Output";
            v << t;
        }

        {
            ToolPreset t = nexus("eslifier", "ESLifier", "MaskPlague", "119846",
                                 "ESLifier.exe", "");
            t.producesOutput = true;
            t.outputModName = "ESLifier Output";
            v << t;
        }

        {
            ToolPreset t = github("pgpatcher", "PGPatcher", "hakasapl", "hakasapl", "PGPatcher",
                                  "PGPatcher-", "PGPatcher.exe", "");
            t.proton = true;
            t.producesOutput = true;
            t.outputModName = "PGPatcher Output";
            v << t;
        }

        {
            ToolPreset t = github("synthesis", "Synthesis", "Mutagen-Modding", "Mutagen-Modding",
                                  "Synthesis", "linux", "Synthesis", "");
            t.proton = false;
            t.producesOutput = true;
            t.outputModName = "Synthesis Output";
            v << t;
        }

        {
            ToolPreset t = github("radium", "Radium Textures", "SulfurNitride",
                                  "SulfurNitride", "Radium-Textures", "radium-textures-linux",
                                  "radium-textures", "");
            t.proton = false;
            t.producesOutput = true;
            t.outputModName = "Radium Output";
            v << t;
        }

        for (auto& t : v) t.iconResource = ":/icons/tools/" + t.id + ".png";
        return v;
    }();
    return p;
}
const ToolPreset* ToolCatalog::byId(const QString& id) {
    for (const auto& t : presets()) if (t.id == id) return &t;
    return nullptr;
}
QString ToolCatalog::dyndolodResourcesModId() { return "52897"; }
}
