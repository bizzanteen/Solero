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
                   "SSEEdit.exe", "");
        v << nexus("xedit-qac", "xEdit - Quick Auto Clean", "ElminsterAU & the xEdit Team", "164",
                   "SSEEditQuickAutoClean.exe", "", {"xedit"});
        v << nexus("dyndolod", "DynDOLOD 3", "Sheson", "68518",
                   "DynDOLODx64.exe", "", {"dyndolod-res"});
        v << nexus("dyndolod-res", "DynDOLOD Resources SE", "Sheson", "52897", "", "");
        v << nexus("texgen", "TexGen (DynDOLOD)", "Sheson", "68518", "TexGenx64.exe", "", {"dyndolod"});
        v << nexus("nemesis", "Nemesis Unlimited Behavior Engine", "ShikyoKira", "60033",
                   "Nemesis Unlimited Behavior Engine.exe", "");
        v << nexus("eslifier", "ESLifier", "Vermunds", "119846", "ESLifier.exe", "");
        v << github("pgpatcher", "ParallaxGen / PGPatcher", "hakasapl", "hakasapl", "ParallaxGen",
                    ".zip", "ParallaxGen.exe", "");
        v << github("synthesis", "Synthesis", "Mutagen-Modding", "Mutagen-Modding", "Synthesis",
                    "Linux", "Synthesis", "");
        v << github("radium", "Radium Textures (VRAMr alt)", "SulfurNitride",
                    "SulfurNitride", "Radium-Textures", "linux", "radium", "");
        return v;
    }();
    return p;
}
const ToolPreset* ToolCatalog::byId(const QString& id) {
    for (const auto& t : presets()) if (t.id == id) return &t;
    return nullptr;
}
}
