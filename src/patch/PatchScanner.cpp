#include "PatchScanner.h"
#include "fomod/FomodEngine.h"
#include "core/Profile.h"
#include "core/Types.h"
#include "install/ModInstaller.h"
#include <QDir>
#include <QFileInfo>
#include <QDomDocument>

namespace solero {

namespace {

struct FileDep { QString file; bool wantsPresent; };

// Recursively collect every <fileDependency> referenced in a stored condition
// XML blob (a <visible> or <dependencyType> element). wantsPresent is false only
// for state="missing" (i.e. the dependency is satisfied by the file being ABSENT)
// - state="inactive" cannot be observed so we also treat it as not-wanting-present.
void collectDepsRec(const QDomElement& el, QList<FileDep>& out) {
    if (el.isNull()) return;
    if (el.tagName().compare("fileDependency", Qt::CaseInsensitive) == 0) {
        const QString state = el.attribute("state").toLower();
        out.append({ el.attribute("file"), state != "missing" && state != "inactive" });
    }
    for (QDomElement c = el.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        collectDepsRec(c, out);
}

QList<FileDep> collectFileDeps(const QString& xml) {
    QList<FileDep> out;
    if (xml.isEmpty()) return out;
    QDomDocument doc;
    if (!doc.setContent(xml)) return out;
    collectDepsRec(doc.documentElement(), out);
    return out;
}

// Case-insensitively resolve `rel` under `base`; empty string if not found.
QString ciResolve(const QString& base, const QString& rel) {
    QStringList parts = rel.split('/', Qt::SkipEmptyParts);
    QString cur = base;
    for (const QString& part : parts) {
        QDir d(cur);
        QString match;
        for (const QString& e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
            if (e.compare(part, Qt::CaseInsensitive) == 0) { match = e; break; }
        if (match.isEmpty()) return {};
        cur = cur + "/" + match;
    }
    return cur;
}

} // namespace

QList<PatchCandidate> candidatesForModule(const FomodModule& module,
                                          const FilePresentFn& filePresent,
                                          const AlreadyInstalledFn& alreadyInstalled,
                                          const PatchModMeta& meta) {
    QList<PatchCandidate> out;
    FomodEngine engine;
    engine.setModule(module);
    engine.setFilePresent(filePresent);

    // MVP: evaluate against the live load order with an empty selection - we do
    // not reconstruct the user's original flag picks, so flag-gated conditions
    // are not satisfied. The "destination files not installed" check (d) is the
    // authoritative "was not installed" signal.
    const FomodEngine::Selection sel;

    for (int si = 0; si < module.steps.size(); ++si) {
        const FomodStep& step = module.steps[si];
        if (!engine.isStepVisible(si, sel)) continue;          // step gated out by current load order
        const QList<FileDep> stepDeps = collectFileDeps(step.visibleConditionXml);

        for (const FomodGroup& grp : step.groups) {
            for (const FomodOption& opt : grp.options) {
                if (opt.files.isEmpty()) continue;             // (a) nothing to install
                if (engine.effectiveType(opt, sel) == OptionType::NotUsable) continue; // (b)

                // (c) relevance: a fileDependency (on the option or its step gate)
                // requiring a now-PRESENT file.
                QList<FileDep> deps = stepDeps;
                deps += collectFileDeps(opt.conditionTypeXml);
                QString satisfied;
                for (const FileDep& d : deps)
                    if (d.wantsPresent && !d.file.isEmpty() && filePresent(d.file)) {
                        satisfied = d.file; break;
                    }
                if (satisfied.isEmpty()) continue;

                // (d) not installed: skip only if every destination file is present.
                bool allInstalled = true;
                for (const FomodFile& f : opt.files)
                    if (!alreadyInstalled(f)) { allInstalled = false; break; }
                if (allInstalled) continue;

                PatchCandidate c;
                c.modId = meta.modId;
                c.modName = meta.modName;
                c.optionName = opt.name;
                c.optionDescription = opt.description;
                c.reason = QStringLiteral("Requires %1 (present)").arg(satisfied);
                c.files = opt.files;
                c.sourceArchive = meta.sourceArchive;
                out.append(c);
            }
        }
    }
    return out;
}

QList<PatchCandidate> scanProfile(const Profile& profile,
                                  const QString& gameDir,
                                  const QString& stagingRoot,
                                  const std::function<void(const QString&)>& progress) {
    QList<PatchCandidate> out;

    // "Present now" predicate - mirrors MainWindow's install-time setFilePresent:
    // present if the file sits in the live game Data dir, or in any enabled mod's
    // staged Data dir (case-insensitive, top-level files only - plugins live at
    // the Data root).
    auto ciFileInDir = [](const QString& dir, const QString& name) {
        QDir d(dir);
        for (const QString& e : d.entryList(QDir::Files))
            if (e.compare(name, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    FilePresentFn filePresent = [&](const QString& file) -> bool {
        if (ciFileInDir(gameDir + "/Data", file)) return true;
        for (const auto& m : profile.modList())
            if (m.type == EntryType::Mod && m.enabled
                && ciFileInDir(stagingRoot + "/" + m.id + "/Data", file))
                return true;
        return false;
    };

    for (const auto& m : profile.modList()) {
        if (m.type != EntryType::Mod || !m.enabled) continue;
        if (!m.hasFomodChoices || m.sourceArchive.isEmpty()) continue;
        if (!QFileInfo::exists(m.sourceArchive)) continue;
        if (progress) progress(m.name);

        InstallPrep prep = ModInstaller::prepare(m.sourceArchive);
        if (!prep.ok || prep.fomodConfigPath.isEmpty()) continue;   // not a (parseable) FOMOD
        FomodEngine engine;
        if (!engine.load(prep.fomodConfigPath)) continue;

        const QString modDataDir = stagingRoot + "/" + m.id + "/Data";
        AlreadyInstalledFn alreadyInstalled = [&](const FomodFile& f) -> bool {
            if (f.isFolder) {
                // A folder installs its CONTENTS into Data/<destination>. With an
                // empty destination it lands at the Data root, which always exists,
                // so we cannot tell - treat as not installed (surface it). Otherwise
                // it is installed if the destination dir exists and is non-empty.
                if (f.destination.isEmpty()) return false;
                QString resolved = ciResolve(modDataDir, f.destination);
                return !resolved.isEmpty() && QFileInfo(resolved).isDir()
                       && !QDir(resolved).isEmpty();
            }
            const QString destRel = f.destination.isEmpty() ? f.source : f.destination;
            return !ciResolve(modDataDir, destRel).isEmpty();
        };

        PatchModMeta meta{ m.id, m.name, m.sourceArchive };
        out += candidatesForModule(engine.module(), filePresent, alreadyInstalled, meta);
        // prep.tempDir (shared_ptr<QTemporaryDir>) auto-removes when prep dies.
    }
    return out;
}

} // namespace solero
