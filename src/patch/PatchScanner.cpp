#include "PatchScanner.h"
#include "fomod/FomodEngine.h"
#include "core/Profile.h"
#include "core/Types.h"
#include "core/AppConfig.h"
#include "install/ModInstaller.h"
#include "install/PluginScanner.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDomDocument>
#include <QSet>
#include <QHash>

namespace solero {

QString normalizeName(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (const QChar c : s)
        if (c.isLetterOrNumber()) out += c.toLower();
    return out;
}

QString normalizePath(const QString& s) {
    QString p = s;
    p.replace('\\', '/');
    return p.toLower();
}

namespace {

// A stable identity for an install operation, used both for the
// collect(original+opt) MINUS collect(original) diff and for cross-candidate
// dedup. Folders are keyed by source+destination (an empty destination is the
// Data root, which would otherwise collide); files by their effective dest path.
QString fileKey(const FomodFile& f) {
    if (f.isFolder)
        return "d|" + normalizePath(f.source) + "|" + normalizePath(f.destination);
    return "f|" + normalizePath(f.destination.isEmpty() ? f.source : f.destination);
}

QSet<QString> keysOf(const QList<FomodFile>& files) {
    QSet<QString> s;
    for (const FomodFile& f : files) s.insert(fileKey(f));
    return s;
}

// Recursively collect every fileDependency referenced in a stored condition XML
// blob (a <visible> or <dependencyType>), returning the file names whose
// "satisfied" state is PRESENT (state=active/missing-negation excluded). Used to
// name the trigger of a direct-file (typeDescriptor-gated) patch.
void collectPresentDepsRec(const QDomElement& el, QStringList& out) {
    if (el.isNull()) return;
    if (el.tagName().compare("fileDependency", Qt::CaseInsensitive) == 0) {
        const QString state = el.attribute("state").toLower();
        if (state != "missing" && state != "inactive")
            out.append(el.attribute("file"));
    }
    for (QDomElement c = el.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        collectPresentDepsRec(c, out);
}

QStringList presentFileDeps(const QString& xml) {
    QStringList out;
    if (xml.isEmpty()) return out;
    QDomDocument doc;
    if (!doc.setContent(xml)) return out;
    collectPresentDepsRec(doc.documentElement(), out);
    return out;
}

// For the file-driven (conditionalFileInstalls) branch: try to name the single
// dependency file that now makes the surfaced files apply. Parses the stored
// <conditionalFileInstalls> XML, finds the <pattern>(s) whose payload overlaps
// `targetKeys`, and returns a present fileDependency gating them - but only when
// exactly one distinct trigger is found (otherwise it isn't unambiguous and we
// fall back to general wording). Empty when not determinable.
QString attributeConditionalTrigger(const QString& conditionalInstallsXml,
                                    const QSet<QString>& targetKeys,
                                    const FilePresentFn& filePresent) {
    if (conditionalInstallsXml.isEmpty()) return {};
    QDomDocument doc;
    if (!doc.setContent(conditionalInstallsXml)) return {};
    QStringList triggers;
    const QDomNodeList patterns = doc.elementsByTagName("pattern");
    for (int i = 0; i < patterns.size(); ++i) {
        const QDomElement pat = patterns.at(i).toElement();
        // Does this pattern's <files> payload overlap the surfaced files?
        bool overlaps = false;
        for (QDomElement files = pat.firstChildElement();
             !overlaps && !files.isNull(); files = files.nextSiblingElement()) {
            if (files.tagName().compare("files", Qt::CaseInsensitive) != 0) continue;
            for (QDomElement n = files.firstChildElement();
                 !n.isNull(); n = n.nextSiblingElement()) {
                FomodFile f;
                f.source = n.attribute("source");
                f.destination = n.attribute("destination");
                f.isFolder = n.tagName().compare("folder", Qt::CaseInsensitive) == 0;
                if (targetKeys.contains(fileKey(f))) { overlaps = true; break; }
            }
        }
        if (!overlaps) continue;
        // Name a present fileDependency that gates this pattern.
        QStringList deps;
        collectPresentDepsRec(pat, deps);
        for (const QString& d : deps)
            if (!d.isEmpty() && filePresent(d) && !triggers.contains(d)) triggers << d;
    }
    return triggers.size() == 1 ? triggers.first() : QString();
}

// Map a flag-setting option onto a present installed mod. Matching is fuzzy and
// case/punctuation-insensitive, in either direction, against the mod's display
// name, its nexus name, and its plugin basenames. A short option-name guard
// avoids pathological substring hits (e.g. an option literally named "All").
//
// LIMITATION: this relies on the option name being a recognisable token for the
// mod (the dominant "pick which mod you have" convention). Authors who name the
// flag/option arbitrarily will not be matched - file-driven detection is the
// backstop for those.
const InstalledModId* mapOptionToMod(const FomodOption& opt,
                                     const QList<InstalledModId>& mods) {
    const QString optNorm = normalizeName(opt.name);
    if (optNorm.size() < 4) return nullptr; // too generic to match safely

    auto relates = [&](const QString& other) {
        const QString o = normalizeName(other);
        if (o.size() < 4) return false;
        return optNorm.contains(o) || o.contains(optNorm);
    };

    for (const InstalledModId& m : mods) {
        if (relates(m.name) || (!m.nexusName.isEmpty() && relates(m.nexusName)))
            return &m;
        for (const QString& pb : m.pluginBasenames) {
            // Compare against the basename sans extension.
            QString stem = pb;
            const int dot = stem.lastIndexOf('.');
            if (dot > 0) stem.truncate(dot);
            if (relates(stem)) return &m;
        }
    }
    return nullptr;
}

} // namespace

QList<PatchCandidate> findPatches(const FomodModule& module,
                                  const FomodEngine::Selection& original,
                                  const FilePresentFn& filePresent,
                                  const AlreadyInstalledFn& alreadyInstalled,
                                  const QList<InstalledModId>& installedMods,
                                  const CollectFn& collect,
                                  const PatchModMeta& meta) {
    QList<PatchCandidate> out;

    // FomodEngine drives visibility / effective-type / flag evaluation against the
    // live load order. collect() is injected separately so the (heavy, already
    // unit-tested) conditionalFileInstalls evaluation is reused, not reimplemented.
    FomodEngine engine;
    engine.setModule(module);
    engine.setFilePresent(filePresent);

    const QList<FomodFile> baseline = collect(original);
    const QSet<QString> baselineKeys = keysOf(baseline);

    QSet<QString> emitted; // cross-candidate dedup by fileKey

    auto makeCandidate = [&](const QString& optName, const QString& desc,
                             const QString& reason, const QList<FomodFile>& files) {
        PatchCandidate c;
        c.modId = meta.modId;
        c.modName = meta.modName;
        c.optionName = optName;
        c.optionDescription = desc;
        c.reason = reason;
        c.files = files;
        c.sourceArchive = meta.sourceArchive;
        c.installable = meta.installable;
        out.append(c);
        for (const FomodFile& f : files) emitted.insert(fileKey(f));
    };

    // Filter a candidate file list to what is not-yet-emitted and not-installed.
    auto pruned = [&](const QList<FomodFile>& files) {
        QList<FomodFile> keep;
        for (const FomodFile& f : files) {
            if (emitted.contains(fileKey(f))) continue;
            if (alreadyInstalled(f)) continue;
            keep.append(f);
        }
        return keep;
    };

    // Per-option passes over reachable, unselected options
    for (int si = 0; si < module.steps.size(); ++si) {
        if (!engine.isStepVisible(si, original)) continue; // step gated out now
        const FomodStep& step = module.steps[si];
        const QStringList stepDeps = presentFileDeps(step.visibleConditionXml);
        for (int gi = 0; gi < step.groups.size(); ++gi) {
            const FomodGroup& grp = step.groups[gi];

            // Is some option in this group already chosen originally? (cardinality)
            bool groupHasSelection = false;
            for (int oi = 0; oi < grp.options.size(); ++oi)
                if (original.value(FomodEngine::selKey(si, gi, oi))) { groupHasSelection = true; break; }
            const bool exclusive = grp.type == GroupType::ExactlyOne
                                || grp.type == GroupType::AtMostOne;

            for (int oi = 0; oi < grp.options.size(); ++oi) {
                const FomodOption& opt = grp.options[oi];
                const QString key = FomodEngine::selKey(si, gi, oi);
                if (original.value(key)) continue;        // already selected -> installed
                if (engine.effectiveType(opt, original) == OptionType::NotUsable) continue;

                // (A) FLAG-DRIVEN: a flag-setting option whose payload lives in
                //     <conditionalFileInstalls>, mapped to a present installed mod.
                if (!opt.flags.isEmpty()) {
                    if (const InstalledModId* m = mapOptionToMod(opt, installedMods)) {
                        FomodEngine::Selection aug = original;
                        aug.insert(key, true);
                        // delta = files unlocked by adding this flag, minus baseline.
                        QList<FomodFile> delta;
                        for (const FomodFile& f : collect(aug))
                            if (!baselineKeys.contains(fileKey(f))) delta.append(f);
                        const QList<FomodFile> keep = pruned(delta);
                        if (!keep.isEmpty()) {
                            QString reason = QStringLiteral("%1 is installed").arg(m->name);
                            if (exclusive && groupHasSelection)
                                reason += QStringLiteral(" (alternative to your current choice)");
                            makeCandidate(opt.name, opt.description, reason, keep);
                        }
                    }
                }

                // (B) DIRECT-file: an option that carries its own files and whose
                //     typeDescriptor (or step gate) is satisfied by a now-present
                //     file (effective type became Required/Recommended).
                if (!opt.files.isEmpty() && !opt.conditionTypeXml.isEmpty()) {
                    const OptionType et = engine.effectiveType(opt, original);
                    if (et == OptionType::Required || et == OptionType::Recommended) {
                        QStringList deps = stepDeps;
                        deps += presentFileDeps(opt.conditionTypeXml);
                        QString trigger;
                        for (const QString& d : deps)
                            if (!d.isEmpty() && filePresent(d)) { trigger = d; break; }
                        if (!trigger.isEmpty()) {
                            const QList<FomodFile> keep = pruned(opt.files);
                            if (!keep.isEmpty())
                                makeCandidate(opt.name, opt.description,
                                    QStringLiteral("Requires %1 (present)").arg(trigger), keep);
                        }
                    }
                }
            }
        }
    }

    // file-DRIVEN: conditional/required files now satisfied but not on disk
    // collect(original) already folds in conditionalFileInstalls whose
    // fileDependency the live load order now satisfies; anything in there that is
    // missing from this mod is a newly-applicable patch.
    QList<FomodFile> fileDriven;
    for (const FomodFile& f : baseline) {
        if (emitted.contains(fileKey(f))) continue;
        if (alreadyInstalled(f)) continue;
        fileDriven.append(f);
    }
    if (!fileDriven.isEmpty()) {
        const QString trigger = attributeConditionalTrigger(
            module.conditionalInstallsXml, keysOf(fileDriven), filePresent);
        const QString reason = trigger.isEmpty()
            ? QStringLiteral("Optional patch files that apply to your current setup")
            : QStringLiteral("Auto-applied patch (needs %1, now present)").arg(trigger);
        makeCandidate(QStringLiteral("Newly applicable files"), QString(),
                      reason, fileDriven);
    }

    return out;
}

// IO wrapper
namespace {

QString childCI(const QString& parent, const QString& name) {
    QDir d(parent);
    if (!d.exists()) return {};
    for (const QString& e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
        if (e.compare(name, Qt::CaseInsensitive) == 0) return parent + "/" + e;
    return {};
}

// Recursive set of normalized relative paths under `dir` (FollowSymlinks).
QSet<QString> recursiveRelSet(const QString& dir) {
    QSet<QString> out;
    QDir base(dir);
    if (!base.exists()) return out;
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        const QString full = it.next();
        out.insert(normalizePath(base.relativeFilePath(full)));
    }
    return out;
}

QStringList topLevelPlugins(const QString& dataDir) {
    QDir d(dataDir);
    if (!d.exists()) return {};
    return d.entryList({"*.esp", "*.esm", "*.esl", "*.ESP", "*.ESM", "*.ESL"},
                       QDir::Files, QDir::Name);
}

// Locate a staged FOMOD config (imports / Wabbajack) case-insensitively.
QString stagedModuleConfig(const QString& modDir) {
    const QString fomod = childCI(modDir, "fomod");
    if (fomod.isEmpty()) return {};
    return childCI(fomod, "ModuleConfig.xml");
}

// Reconstruct the original Selection from the fomod-choices log by matching stored
// per-step option NAMES back to the module. Name-based matching is the only signal
// the log stores (selKey indices are not persisted), so an authoring change to an
// option name between install and scan would silently drop that pick.
FomodEngine::Selection reconstructSelection(const FomodModule& module,
                                            const QString& choicesPath) {
    FomodEngine::Selection sel;
    QFile f(choicesPath);
    if (!f.open(QIODevice::ReadOnly)) return sel;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray steps = root.value("steps").toArray();

    QHash<QString, QSet<QString>> byStep; // lowercased step name -> selected option names
    for (const QJsonValue& sv : steps) {
        const QJsonObject so = sv.toObject();
        const QString sname = so.value("step").toString().toLower();
        QSet<QString>& names = byStep[sname];
        for (const QJsonValue& nv : so.value("selected").toArray())
            names.insert(nv.toString());
    }

    for (int si = 0; si < module.steps.size(); ++si) {
        const auto it = byStep.constFind(module.steps[si].name.toLower());
        if (it == byStep.constEnd()) continue;
        const QSet<QString>& names = it.value();
        for (int gi = 0; gi < module.steps[si].groups.size(); ++gi)
            for (int oi = 0; oi < module.steps[si].groups[gi].options.size(); ++oi)
                if (names.contains(module.steps[si].groups[gi].options[oi].name))
                    sel.insert(FomodEngine::selKey(si, gi, oi), true);
    }
    return sel;
}

} // namespace

QList<PatchCandidate> scanProfile(const Profile& profile,
                                  const QString& gameDir,
                                  const QString& stagingRoot,
                                  const std::function<void(const QString&)>& progress) {
    QList<PatchCandidate> out;

    // Presence model (built once, reused for every mod)
    // Plugins: case-insensitive basenames from enabled mods + game Data.
    QSet<QString> plugins;
    for (const QString& p : PluginScanner::scan(profile.modList(), stagingRoot))
        plugins.insert(p.toLower());
    for (const QString& p : PluginScanner::scanGameData(gameDir))
        plugins.insert(p.toLower());

    // Loose files: case-insensitive normalized relative Data paths (recursive)
    // across every enabled mod + the live game Data.
    QSet<QString> looseFiles = recursiveRelSet(gameDir + "/Data");
    for (const auto& m : profile.modList()) {
        if (m.type != EntryType::Mod || !m.enabled) continue;
        const QString data = childCI(stagingRoot + "/" + m.id, "Data");
        if (!data.isEmpty()) looseFiles.unite(recursiveRelSet(data));
    }

    FilePresentFn filePresent = [&](const QString& file) -> bool {
        const QString p = normalizePath(file);
        const QString base = p.section('/', -1);
        if (plugins.contains(base)) return true;
        if (looseFiles.contains(p)) return true;
        return false;
    };

    // Installed-mod identities for flag-option ↔ mod mapping.
    QList<InstalledModId> installedMods;
    for (const auto& m : profile.modList()) {
        if (m.type != EntryType::Mod || !m.enabled || m.isOutputMod) continue;
        InstalledModId id;
        id.modId = m.id;
        id.name = m.name;
        id.nexusName = m.name; // we only persist a numeric nexus id; reuse display name
        id.pluginBasenames = topLevelPlugins(childCI(stagingRoot + "/" + m.id, "Data"));
        id.normalizedName = normalizeName(m.name);
        installedMods.append(id);
    }

    const QString choicesDir = AppConfig::dataRoot() + "/fomod-choices";

    for (const auto& m : profile.modList()) {
        if (m.type != EntryType::Mod || !m.enabled || m.isOutputMod) continue;
        if (!m.hasFomodChoices) continue; // only FOMOD-installed mods carry patches
        if (progress) progress(m.name);

        const QString modDir = stagingRoot + "/" + m.id;

        // Prefer the source archive (we can install from it). Fall back to a staged
        // fomod/ModuleConfig.xml (imports/Wabbajack) - surface-only, no install.
        QString configPath;
        bool installable = false;
        InstallPrep prep; // keep the temp extraction alive until we have loaded the XML
        if (!m.sourceArchive.isEmpty() && QFileInfo::exists(m.sourceArchive)) {
            prep = ModInstaller::prepare(m.sourceArchive);
            if (prep.ok && !prep.fomodConfigPath.isEmpty()) {
                configPath = prep.fomodConfigPath;
                installable = true;
            }
        }
        if (configPath.isEmpty()) {
            const QString staged = stagedModuleConfig(modDir);
            if (!staged.isEmpty()) configPath = staged; // installable stays false
        }
        if (configPath.isEmpty()) continue; // no parseable FOMOD source - skip

        FomodEngine engine;
        if (!engine.load(configPath)) continue;
        engine.setFilePresent(filePresent);

        const FomodEngine::Selection original =
            reconstructSelection(engine.module(), choicesDir + "/" + m.id + ".json");

        CollectFn collect = [&engine](const FomodEngine::Selection& sel) {
            return engine.collectFiles(sel);
        };

        // Per-mod "already installed?" predicate over the mod's staged Data tree.
        const QString modData = childCI(modDir, "Data");
        const QSet<QString> modFiles = recursiveRelSet(modData);
        AlreadyInstalledFn alreadyInstalled = [&modFiles](const FomodFile& f) -> bool {
            if (f.isFolder) {
                // A folder installs its CONTENTS under Data/<destination>. With an
                // empty destination it lands at the Data root and we cannot tell
                // which files it carries (would need to list the archive), so we
                // treat it as not installed (idempotent re-copy if surfaced).
                if (f.destination.isEmpty()) return false;
                const QString d = normalizePath(f.destination);
                for (const QString& rel : modFiles)
                    if (rel == d || rel.startsWith(d + "/")) return true;
                return false;
            }
            return modFiles.contains(
                normalizePath(f.destination.isEmpty() ? f.source : f.destination));
        };

        PatchModMeta meta{ m.id, m.name, installable ? m.sourceArchive : QString(), installable };
        out += findPatches(engine.module(), original, filePresent, alreadyInstalled,
                           installedMods, collect, meta);
        // prep (shared_ptr<QTemporaryDir>) auto-removes when it goes out of scope.
    }
    return out;
}

} // namespace solero
