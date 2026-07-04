#include "PatchScanner.h"
#include "fomod/FomodEngine.h"
#include "fomod/FomodScanner.h"
#include "core/Profile.h"
#include "core/Types.h"
#include "core/AppConfig.h"
#include "core/StagingFolder.h"
#include "install/ArchiveTool.h"
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

// For the file-driven (conditionalFileInstalls) branch: name every present
// dependency file that now makes the surfaced files apply. Parses the stored
// <conditionalFileInstalls> XML, finds the <pattern>(s) whose payload overlaps
// `targetKeys`, and returns the present fileDependencies gating them (deduped,
// in document order). Empty when none are determinable.
QStringList presentConditionalTriggers(const QString& conditionalInstallsXml,
                                       const QSet<QString>& targetKeys,
                                       const FilePresentFn& filePresent) {
    QStringList triggers;
    if (conditionalInstallsXml.isEmpty()) return triggers;
    QDomDocument doc;
    if (!doc.setContent(conditionalInstallsXml)) return triggers;
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
        // Name every present fileDependency that gates this pattern.
        QStringList deps;
        collectPresentDepsRec(pat, deps);
        for (const QString& d : deps)
            if (!d.isEmpty() && filePresent(d) && !triggers.contains(d)) triggers << d;
    }
    return triggers;
}

// Name a file-driven candidate from its payload so the row is self-describing:
// prefer the plugin(s) it installs, else a sample file/folder plus a count.
QString describePatchPayload(const QList<FomodFile>& files) {
    auto baseName = [](const FomodFile& f) {
        QString d = f.destination.isEmpty() ? f.source : f.destination;
        return d.replace('\\', '/').section('/', -1);
    };
    QStringList plugins;
    for (const FomodFile& f : files) {
        if (f.isFolder) continue;
        const QString b = baseName(f);
        const QString l = b.toLower();
        if ((l.endsWith(".esp") || l.endsWith(".esm") || l.endsWith(".esl"))
            && !plugins.contains(b))
            plugins << b;
    }
    if (!plugins.isEmpty()) {
        const int shown = qMin(plugins.size(), 2);
        QString s = plugins.mid(0, shown).join(", ");
        if (plugins.size() > shown)
            s += QStringLiteral(" (+%1 more)").arg(plugins.size() - shown);
        return s;
    }
    // No plugin payload: sample the first entry and note the file count.
    QString sample;
    for (const FomodFile& f : files) {
        sample = f.isFolder ? (baseName(f) + "/") : baseName(f);
        if (!sample.isEmpty() && sample != "/") break;
    }
    if (sample.isEmpty()) sample = QStringLiteral("patch files");
    const int n = files.size();
    return n > 1 ? QStringLiteral("%1 (+%2 files)").arg(sample).arg(n - 1) : sample;
}

} // namespace

QList<PatchCandidate> findPatches(const FomodModule& module,
                                  const FomodEngine::Selection& original,
                                  const FilePresentFn& filePresent,
                                  const AlreadyInstalledFn& alreadyInstalled,
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
        c.stagingDir = meta.stagingDir;
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

    // DIRECT-file: options carrying their own files gated by a typeDescriptor
    // whose fileDependency is now satisfied (effective type Required/Recommended).
    for (int si = 0; si < module.steps.size(); ++si) {
        if (!engine.isStepVisible(si, original)) continue; // step gated out now
        const FomodStep& step = module.steps[si];
        const QStringList stepDeps = presentFileDeps(step.visibleConditionXml);
        for (int gi = 0; gi < step.groups.size(); ++gi) {
            const FomodGroup& grp = step.groups[gi];
            for (int oi = 0; oi < grp.options.size(); ++oi) {
                const FomodOption& opt = grp.options[oi];
                const QString key = FomodEngine::selKey(si, gi, oi);
                if (original.value(key)) continue;        // already selected -> installed
                if (engine.effectiveType(opt, original) == OptionType::NotUsable) continue;

                if (opt.files.isEmpty() || opt.conditionTypeXml.isEmpty()) continue;
                const OptionType et = engine.effectiveType(opt, original);
                if (et != OptionType::Required && et != OptionType::Recommended) continue;

                QStringList deps = stepDeps;
                deps += presentFileDeps(opt.conditionTypeXml);
                QString trigger;
                for (const QString& d : deps)
                    if (!d.isEmpty() && filePresent(d)) { trigger = d; break; }
                if (trigger.isEmpty()) continue; // no nameable present trigger -> drop
                const QList<FomodFile> keep = pruned(opt.files);
                if (!keep.isEmpty())
                    makeCandidate(opt.name, opt.description,
                        QStringLiteral("Requires %1 (present)").arg(trigger), keep);
            }
        }
    }

    // CONDITIONAL: conditionalFileInstalls files now satisfied but not on disk
    // collect(original) already folds in conditionalFileInstalls whose
    // fileDependency the live load order now satisfies; anything in there that is
    // missing from this mod is a newly-applicable patch - BUT only when a concrete
    // present trigger can be named. A payload gated solely by flags (no
    // fileDependency) is suppressed (the Embers-XD false-positive class).
    QList<FomodFile> fileDriven;
    for (const FomodFile& f : baseline) {
        if (emitted.contains(fileKey(f))) continue;
        if (alreadyInstalled(f)) continue;
        fileDriven.append(f);
    }
    if (!fileDriven.isEmpty()) {
        const QStringList triggers = presentConditionalTriggers(
            module.conditionalInstallsXml, keysOf(fileDriven), filePresent);
        if (!triggers.isEmpty()) {
            const int shown = qMin(triggers.size(), 3);
            QString t = triggers.mid(0, shown).join(", ");
            if (triggers.size() > shown)
                t += QStringLiteral(" +%1 more").arg(triggers.size() - shown);
            // Name the row by what it actually installs (the patch plugin/file).
            makeCandidate(describePatchPayload(fileDriven), QString(),
                          QStringLiteral("needs %1 (present)").arg(t), fileDriven);
        }
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

// CRC32 (IEEE, poly 0xEDB88320) over a file's bytes - matches zip/7z stored CRC.
// Used to disambiguate same-path FOMOD option variants during reconstruction.
quint32 crc32File(const QString& path) {
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    quint32 crc = 0xFFFFFFFFu;
    char buf[1 << 16];
    qint64 n;
    while ((n = f.read(buf, sizeof(buf))) > 0)
        for (qint64 i = 0; i < n; ++i)
            crc = table[(crc ^ static_cast<quint8>(buf[i])) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// Locate a staged FOMOD config (imports / Wabbajack) case-insensitively.
QString stagedModuleConfig(const QString& modDir) {
    const QString fomod = childCI(modDir, "fomod");
    if (fomod.isEmpty()) return {};
    return childCI(fomod, "ModuleConfig.xml");
}

// Map per-step selected option NAMES (lowercased step name -> names) back to a
// module Selection. Name-based matching is the only signal both the fomod-choices
// log and the file-diff reconstruction store (selKey indices are not persisted),
// so an authoring change to an option name would silently drop that pick.
FomodEngine::Selection selectionFromStepNames(
    const FomodModule& module, const QHash<QString, QSet<QString>>& byStep) {
    FomodEngine::Selection sel;
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

// Reconstruct the original Selection from a Solero fomod-choices log.
FomodEngine::Selection selectionFromLog(const FomodModule& module,
                                        const QString& choicesPath) {
    QFile f(choicesPath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QHash<QString, QSet<QString>> byStep; // lowercased step name -> selected option names
    for (const QJsonValue& sv : root.value("steps").toArray()) {
        const QJsonObject so = sv.toObject();
        QSet<QString>& names = byStep[so.value("step").toString().toLower()];
        for (const QJsonValue& nv : so.value("selected").toArray())
            names.insert(nv.toString());
    }
    return selectionFromStepNames(module, byStep);
}

// Reconstruct an IMPORTED mod's selection by diffing its staged Data tree against
// the FOMOD option file-sets (via FomodScanner's file-diff core). `status`, if
// non-null, receives the reconstruction annotation ("reconstructed"/"needs-rerun").
FomodEngine::Selection reconstructImportedSelection(const FomodModule& module,
                                                    const QString& sourceArchive,
                                                    const QString& modData,
                                                    QString* status) {
    if (status) status->clear();
    if (sourceArchive.isEmpty() || !QFileInfo::exists(sourceArchive)) return {};

    // Flag-driven installers cannot be reconstructed by file-diff (same files
    // install regardless of which flag) - annotate and give up.
    if (classifyModule(module) == FomodClass::FlagDriven) {
        if (status) *status = QStringLiteral("needs-rerun");
        return {};
    }

    bool ok = false;
    const QList<ArchiveTool::Entry> entries =
        ArchiveTool::listEntriesWithCrc(sourceArchive, &ok);
    if (!ok || entries.isEmpty()) {
        if (status) *status = QStringLiteral("needs-rerun");
        return {};
    }

    // Present-file model + CRC lookup over the mod's staged Data tree.
    QSet<QString> installed;
    QHash<QString, QString> realByLower;
    if (!modData.isEmpty()) {
        QDir base(modData);
        QDirIterator it(modData, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext()) {
            const QString full = it.next();
            const QString rel = normalizePath(base.relativeFilePath(full));
            installed.insert(rel);
            realByLower.insert(rel, full);
        }
    }
    QHash<QString, quint32> crcCache;
    auto crcFn = [&](const QString& relLower) -> quint32 {
        const auto c = crcCache.constFind(relLower);
        if (c != crcCache.constEnd()) return c.value();
        quint32 v = 0;
        const auto r = realByLower.constFind(relLower);
        if (r != realByLower.constEnd()) v = crc32File(r.value());
        crcCache.insert(relLower, v);
        return v;
    };

    QList<FomodArchiveEntry> fae;
    fae.reserve(entries.size());
    for (const ArchiveTool::Entry& e : entries) fae.append({ e.path, e.crc });

    const ReconstructResult rec = reconstructSelection(module, fae, installed, crcFn);
    if (status) *status = QStringLiteral("reconstructed");

    QHash<QString, QSet<QString>> byStep;
    for (const ReconstructedStep& rs : rec.steps) {
        QSet<QString>& names = byStep[rs.step.toLower()];
        for (const QString& nm : rs.selected) names.insert(nm);
    }
    return selectionFromStepNames(module, byStep);
}

} // namespace

FomodEngine::Selection establishSelection(
    const FomodModule& module,
    const QString& choicesLogPath,
    const std::function<FomodEngine::Selection()>& reconstruct,
    bool& reconstructed) {
    // Solero-installed mods have a fomod-choices log -> trust it verbatim; never
    // reconstruct. Imported mods (no log) fall back to file-diff reconstruction.
    if (QFileInfo::exists(choicesLogPath)) {
        reconstructed = false;
        return selectionFromLog(module, choicesLogPath);
    }
    reconstructed = true;
    return reconstruct ? reconstruct() : FomodEngine::Selection{};
}

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
        const QString data = childCI(stagingPathFor(stagingRoot, m), "Data");
        if (!data.isEmpty()) looseFiles.unite(recursiveRelSet(data));
    }

    FilePresentFn filePresent = [&](const QString& file) -> bool {
        const QString p = normalizePath(file);
        const QString base = p.section('/', -1);
        if (plugins.contains(base)) return true;
        if (looseFiles.contains(p)) return true;
        return false;
    };

    const QString choicesDir = AppConfig::dataRoot() + "/fomod-choices";

    for (const auto& m : profile.modList()) {
        if (m.type != EntryType::Mod || !m.enabled || m.isOutputMod) continue;
        if (!m.hasFomodChoices) continue; // only FOMOD-installed mods carry patches
        if (progress) progress(m.name);

        const QString modDir = stagingPathFor(stagingRoot, m);

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

        // Establish the original selection: Solero-installed mods carry a
        // fomod-choices log (trusted verbatim); imported mods have none, so
        // reconstruct via file-diff (FomodScanner core) - only then.
        const QString modData = childCI(modDir, "Data");
        bool reconstructed = false;
        const QString archiveForRec = installable ? m.sourceArchive : QString();
        const FomodEngine::Selection original = establishSelection(
            engine.module(), choicesDir + "/" + m.id + ".json",
            [&]() {
                return reconstructImportedSelection(engine.module(), archiveForRec,
                                                    modData, nullptr);
            },
            reconstructed);

        CollectFn collect = [&engine](const FomodEngine::Selection& sel) {
            return engine.collectFiles(sel);
        };

        // "Already installed?" - check the DESTINATION against the whole live load
        // order (deployed game Data + every enabled mod's staged Data), not just
        // this mod's source tree. An already-applied patch must not re-surface.
        const QSet<QString> modFiles = recursiveRelSet(modData);
        AlreadyInstalledFn alreadyInstalled = [&](const FomodFile& f) -> bool {
            if (f.isFolder) {
                // A folder installs its CONTENTS under Data/<destination>. An empty
                // destination lands at the Data root and we cannot enumerate its
                // members, so treat it as not installed (idempotent re-copy).
                if (f.destination.isEmpty()) return false;
                const QString d = normalizePath(f.destination);
                if (looseFiles.contains(d)) return true;
                for (const QString& rel : looseFiles)
                    if (rel.startsWith(d + "/")) return true;
                for (const QString& rel : modFiles)
                    if (rel == d || rel.startsWith(d + "/")) return true;
                return false;
            }
            const QString dest = f.destination.isEmpty() ? f.source : f.destination;
            return filePresent(dest)
                || modFiles.contains(normalizePath(dest));
        };

        PatchModMeta meta{ m.id, m.name, installable ? m.sourceArchive : QString(),
                           installable, QDir(modDir).exists() ? modDir : QString() };
        if (meta.stagingDir.isEmpty()) meta.installable = false;
        out += findPatches(engine.module(), original, filePresent, alreadyInstalled,
                           collect, meta);
        // prep (shared_ptr<QTemporaryDir>) auto-removes when it goes out of scope.
    }
    return out;
}

} // namespace solero
