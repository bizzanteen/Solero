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
            t.authorUrl="https://github.com/"+owner;
            t.exeRelPath=exe; t.args=args; t.proton=true; return t;
        };

        {
            ToolPreset t = nexus("xedit", "SSEEdit (xEdit)", "ElminsterAU & the xEdit Team", "164",
                                 "SSEEdit.exe", "-sse");
            t.description = "Edit, clean, conflict-check and patch Skyrim plugins (.esp/.esm).";
            t.docsUrl = "https://tes5edit.github.io/docs/";
            t.authorUrl = "https://www.nexusmods.com/users/167469";
            t.extraActions = {
                { "Quick Auto Clean", "SSEEditQuickAutoClean.exe", "-sse", "" },
                { "Quick Show Conflicts", "SSEEdit.exe", "-sse -quickshowconflicts", "" },
            };
            v << t;
        }

        {
            ToolPreset t = nexus("dyndolod", "DynDOLOD", "Sheson", "68518",
                                 "DynDOLODx64.exe", "-sse");
            t.description = "Generate distant LOD for objects, trees and terrain - crisp far views.";
            t.docsUrl = "https://dyndolod.info/";
            t.authorUrl = "https://www.nexusmods.com/users/3155782";
            t.producesOutput = true;
            t.outputModName = "DynDOLOD Output";
            t.extraActions = {{ "Run TexGen", "TexGenx64.exe", "-sse", "TexGen Output" }};
            v << t;
        }

        {
            ToolPreset t = nexus("nemesis", "Nemesis", "ShikyoKira", "60033",
                                 "Nemesis Unlimited Behavior Engine.exe", "");
            t.description = "Animation behavior engine - patches behaviors so animation mods work together.";
            t.docsUrl = "https://github.com/ShikyoKira/Project-New-Reign---Nemesis-Main";
            t.authorUrl = "https://www.nexusmods.com/users/16675984";
            t.producesOutput = true;
            t.outputModName = "Nemesis Output";
            t.runsFromDataDir = true; // ERROR(1008) unless it runs from inside Data
            v << t;
        }

        {
            // 5.8.x is 64-bit only and ships plain BodySlide.exe; the "BodySlide
            // x64.exe" name belongs to older releases.
            ToolPreset t = nexus("bodyslide", "BodySlide and Outfit Studio",
                                 "Ousnius & Caliente", "201",
                                 "CalienteTools/BodySlide/BodySlide.exe", "");
            t.description = "Build body meshes and outfits to your chosen shape and presets.";
            t.docsUrl = "https://github.com/ousnius/BodySlide-and-Outfit-Studio";
            t.authorUrl = "https://www.nexusmods.com/users/4448560";
            t.producesOutput = true;
            t.outputModName = "BodySlide Output";
            // BodySlide reads the SliderSets/ShapeData that body mods (CBBE, HIMBO)
            // install into Data/CalienteTools/BodySlide, and writes the meshes it
            // builds back into Data. Mirroring it into Data puts it alongside that
            // mod data, which is exactly the layout it expects.
            t.runsFromDataDir = true;
            // Outfit Studio ships alongside it and needs the same Data placement.
            t.extraActions = {{ "Open Outfit Studio",
                                "CalienteTools/BodySlide/OutfitStudio.exe", "",
                                "BodySlide Output" }};
            v << t;
        }

        {
            ToolPreset t = github("pandora", "Pandora Behaviour Engine", "Monitor221hb",
                                   "Monitor221hz", "Pandora-Behaviour-Engine-Plus",
                                   "Pandora_Behaviour_Engine", "Pandora Behaviour Engine+.exe", "");
            t.description = "Fast multi-threaded animation behaviour engine (Nemesis alternative).";
            t.docsUrl = "https://github.com/Monitor221hz/Pandora-Behaviour-Engine-Plus";
            t.authorUrl = "https://github.com/Monitor221hz";
            t.proton = true;
            t.producesOutput = true;
            t.outputModName = "Pandora Output";
            v << t;
        }

        {
            ToolPreset t = nexus("eslifier", "ESLifier", "MaskPlague", "145168",
                                 "ESLifier.exe", "");
            t.description = "Compact and ESL-flag plugins to save load-order slots.";
            t.docsUrl = "https://github.com/MaskPlague/ESLifier";
            t.authorUrl = "https://www.nexusmods.com/users/34479385";
            t.producesOutput = true;
            t.outputModName = "ESLifier Output";
            v << t;
        }

        {
            ToolPreset t = github("pgpatcher", "PGPatcher", "hakasapl", "hakasapl", "PGPatcher",
                                  "PGPatcher-", "PGPatcher.exe", "--ignore-mo2vfscheck");
            t.description = "Auto-patch meshes for parallax / complex-material PBR shaders.";
            t.docsUrl = "https://github.com/hakasapl/PGPatcher";
            t.proton = true;
            t.producesOutput = true;
            t.outputModName = "PGPatcher Output";
            // Reads the deployed meshes and the fake-MO2 mod list, so its own
            // previous output has to be out of both or it re-patches it.
            t.clearsOutputBeforeRun = true;
            v << t;
        }

        {
            ToolPreset t = github("synthesis", "Synthesis", "Mutagen-Modding", "Mutagen-Modding",
                                  "Synthesis", "Synthesis", "Synthesis.exe", "");
            t.description = "Run automated, modular load-order patchers (Mutagen-based).";
            t.docsUrl = "https://github.com/Mutagen-Modding/Synthesis";
            t.proton = true;  // ship the Windows build (Synthesis.exe via Proton)
            t.needsDotNet = true; // framework-dependent .NET app - needs the .NET Desktop Runtime in the prefix
            t.producesOutput = true;
            t.outputModName = "Synthesis Output";
            v << t;
        }

        {
            ToolPreset t = github("radium", "Radium Textures", "SulfurNitride",
                                  "SulfurNitride", "Radium-Textures", "radium-textures-linux",
                                  "radium-textures", "");
            t.description = "Optimize and compress textures to cut VRAM use (VRAMr alternative).";
            t.docsUrl = "https://github.com/SulfurNitride/Radium-Textures";
            t.proton = false;
            t.producesOutput = true;
            t.outputModName = "Radium Output";
            t.writesOutputDirectly = true;
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
