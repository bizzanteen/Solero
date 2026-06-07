#include "FomodScanner.h"
#include "FomodEngine.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include "core/Types.h"
#include "install/ArchiveLocator.h"
#include "install/ArchiveTool.h"
#include "install/ModInstaller.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QVector>

namespace solero {

namespace {

QString lowerPath(QString p) { p.replace('\\', '/'); return p.toLower(); }

// Case-insensitive child lookup; returns the real-cased path.
QString childCI(const QString& parent, const QString& name) {
    QDir d(parent);
    if (!d.exists()) return {};
    for (const QString& e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
        if (e.compare(name, Qt::CaseInsensitive) == 0) return parent + "/" + e;
    return {};
}

// CRC32 (IEEE, poly 0xEDB88320) over a file's bytes - matches zip/7z stored CRC.
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

// A concrete destination path (Data-relative, lower-cased) + the source archive
// entry's CRC, for one expanded option file/folder member.
struct DestEntry { QString dest; quint32 crc = 0; };

} // namespace

FomodClass classifyModule(const FomodModule& module) {
    int withFiles = 0, flagOnly = 0;
    for (const auto& step : module.steps)
        for (const auto& grp : step.groups)
            for (const auto& opt : grp.options) {
                if (!opt.files.isEmpty()) ++withFiles;
                else if (!opt.flags.isEmpty()) ++flagOnly;
            }
    // No option carries its own files, yet options set flags and the payloads live
    // in <conditionalFileInstalls> -> a "pick which mod you have" installer whose
    // selection file-diff cannot recover (the same files install regardless).
    if (withFiles == 0 && flagOnly > 0 && !module.conditionalInstallsXml.isEmpty())
        return FomodClass::FlagDriven;
    return FomodClass::DirectFile;
}

ReconstructResult reconstructSelection(
    const FomodModule& module,
    const QList<FomodArchiveEntry>& archiveEntries,
    const QSet<QString>& installedRelPaths,
    const std::function<quint32(const QString&)>& installedCrc) {

    ReconstructResult result;

    // 1) Determine the wrapper prefix before fomod/ModuleConfig.xml, then map every
    //    archive entry to a fomod-relative (lower-cased) path + CRC.
    QString prefix;
    for (const FomodArchiveEntry& e : archiveEntries) {
        const QString l = lowerPath(e.path);
        const int idx = l.indexOf("fomod/moduleconfig.xml");
        if (idx == 0) { prefix.clear(); break; }
        if (idx > 0 && l.at(idx - 1) == '/') { prefix = l.left(idx); break; }
    }
    QHash<QString, quint32> relCrc;
    QList<QPair<QString, quint32>> relList;
    relList.reserve(archiveEntries.size());
    for (const FomodArchiveEntry& e : archiveEntries) {
        QString r = lowerPath(e.path);
        if (!prefix.isEmpty() && r.startsWith(prefix)) r = r.mid(prefix.size());
        relCrc.insert(r, e.crc);
        relList.append({ r, e.crc });
    }

    // 2) Expand an option's <file>/<folder> entries to concrete destinations.
    auto expand = [&](const FomodOption& opt) {
        QList<DestEntry> out;
        for (const FomodFile& f : opt.files) {
            const QString src = lowerPath(f.source);
            if (!f.isFolder) {
                const QString dest = lowerPath(f.destination.isEmpty() ? f.source : f.destination);
                out.append({ dest, relCrc.value(src, 0) });
            } else {
                const QString base = src.isEmpty() ? QString() : (src + "/");
                const QString destBase = lowerPath(f.destination);
                for (const auto& pr : relList) {
                    const QString& r = pr.first;
                    if (!base.isEmpty() && !r.startsWith(base)) continue;
                    const QString tail = base.isEmpty() ? r : r.mid(base.size());
                    if (tail.isEmpty()) continue;
                    out.append({ destBase.isEmpty() ? tail : (destBase + "/" + tail), pr.second });
                }
            }
        }
        return out;
    };

    // 3) Per step -> group -> option, decide the selection.
    for (const FomodStep& step : module.steps) {
        ReconstructedStep rstep;
        rstep.step = step.name;
        for (const FomodGroup& grp : step.groups) {
            const int n = grp.options.size();
            QVector<QList<DestEntry>> dests(n);
            QHash<QString, int> destCount; // how many options touch each dest (this group)
            for (int oi = 0; oi < n; ++oi) {
                dests[oi] = expand(grp.options[oi]);
                QSet<QString> seen;
                for (const DestEntry& de : dests[oi])
                    if (!seen.contains(de.dest)) { seen.insert(de.dest); destCount[de.dest]++; }
            }

            // # of present destinations for an option (optionally unique-only).
            auto presentCount = [&](int oi, bool uniqueOnly) {
                int c = 0; QSet<QString> seen;
                for (const DestEntry& de : dests[oi]) {
                    if (seen.contains(de.dest)) continue;
                    seen.insert(de.dest);
                    if (uniqueOnly && destCount.value(de.dest) != 1) continue;
                    if (installedRelPaths.contains(de.dest)) ++c;
                }
                return c;
            };
            // CRC agreement over an option's present destinations.
            auto crcScore = [&](int oi, int& matches, int& mism) {
                matches = 0; mism = 0; QSet<QString> seen;
                for (const DestEntry& de : dests[oi]) {
                    if (seen.contains(de.dest)) continue;
                    seen.insert(de.dest);
                    if (!installedRelPaths.contains(de.dest)) continue;
                    const quint32 disk = installedCrc ? installedCrc(de.dest) : 0;
                    if (disk == 0 || de.crc == 0) continue; // unknown either side
                    if (disk == de.crc) ++matches; else ++mism;
                }
            };

            QVector<bool> sel(n, false);
            const GroupType gt = grp.type;

            if (gt == GroupType::All) {
                for (int oi = 0; oi < n; ++oi) sel[oi] = true; // every option forced
            } else if (gt == GroupType::Any || gt == GroupType::AtLeastOne) {
                for (int oi = 0; oi < n; ++oi) {
                    if (presentCount(oi, /*uniqueOnly=*/true) > 0) { sel[oi] = true; continue; }
                    if (!dests[oi].isEmpty() && presentCount(oi, false) > 0) {
                        int m, mm; crcScore(oi, m, mm);
                        if (m > 0 && mm == 0) { sel[oi] = true; result.ambiguous = true; }
                    }
                }
            } else { // ExactlyOne / AtMostOne
                int best = -1, bestU = 0;
                for (int oi = 0; oi < n; ++oi) {
                    const int u = presentCount(oi, true);
                    if (u > bestU) { bestU = u; best = oi; }
                }
                if (best >= 0 && bestU > 0) {
                    sel[best] = true;
                } else {
                    // Same-path variants: disambiguate by CRC (single zero-mismatch winner).
                    int cbest = -1, cbestM = 0; bool tie = false;
                    for (int oi = 0; oi < n; ++oi) {
                        int m, mm; crcScore(oi, m, mm);
                        if (mm == 0 && m > 0) {
                            if (m > cbestM) { cbestM = m; cbest = oi; tie = false; }
                            else if (m == cbestM) tie = true;
                        }
                    }
                    if (cbest >= 0 && !tie) { sel[cbest] = true; result.ambiguous = true; }
                    else if (gt == GroupType::ExactlyOne) {
                        // Infer the empty/"none"/"default" option by elimination.
                        int none = -1;
                        for (int oi = 0; oi < n && none < 0; ++oi)
                            if (dests[oi].isEmpty()) none = oi;     // file-less = "none"
                        for (int oi = 0; oi < n && none < 0; ++oi) {
                            const QString nm = grp.options[oi].name.toLower();
                            if (nm.contains("none") || nm.contains("vanilla")
                                || nm.contains("default") || nm.contains("do not")
                                || nm.startsWith("no "))
                                none = oi;
                        }
                        if (none >= 0) { sel[none] = true; result.ambiguous = true; }
                    }
                }
            }

            for (int oi = 0; oi < n; ++oi)
                if (sel[oi]) rstep.selected << grp.options[oi].name;
        }
        result.steps.append(rstep);
    }
    return result;
}

FomodScanSummary scanProfile(Profile& profile,
                             const QString& gameDir,
                             const QString& stagingRoot,
                             const std::function<void(int, int, const QString&)>& progress,
                             const std::function<bool()>& isCancelled) {
    Q_UNUSED(gameDir);
    FomodScanSummary sum;

    ArchiveLocator locator(stagingRoot);
    locator.discoverInstanceDownloadDirs(profile);

    // Snapshot the ids to scan (so writes via findById don't disturb iteration).
    QStringList ids;
    for (const auto& m : profile.modList())
        if (m.type == EntryType::Mod && m.enabled && !m.isOutputMod) ids << m.id;

    const int total = ids.size();
    const QString choicesDir = AppConfig::dataRoot() + "/fomod-choices";
    bool dirty = false;
    int done = 0;

    for (const QString& id : ids) {
        if (isCancelled && isCancelled()) break;
        ModEntry* mod = profile.modList().findById(id);
        if (!mod) { ++done; continue; }
        if (progress) progress(done, total, mod->name);
        ++done;
        ++sum.scanned;

        const QString archive = locator.locate(*mod);
        if (archive.isEmpty()) { ++sum.archiveNotFound; continue; }

        bool ok = false;
        const QList<ArchiveTool::Entry> entries = ArchiveTool::listEntriesWithCrc(archive, &ok);
        if (!ok || entries.isEmpty()) { ++sum.archiveNotFound; continue; }

        // FOMOD signal: fomod/moduleconfig.xml at any depth, case-insensitive.
        bool isFo = false;
        for (const ArchiveTool::Entry& e : entries) {
            const QString l = lowerPath(e.path);
            if (l == "fomod/moduleconfig.xml" || l.endsWith("/fomod/moduleconfig.xml")) {
                isFo = true; break;
            }
        }
        if (!isFo) {
            if (mod->isFomod) { mod->isFomod = false; dirty = true; }
            continue;
        }

        ++sum.fomodFound;
        if (!mod->isFomod) { mod->isFomod = true; dirty = true; }
        if (mod->sourceArchive != archive) { mod->sourceArchive = archive; dirty = true; }

        // Parse the FOMOD module (extracts fomod/ from the archive).
        InstallPrep prep = ModInstaller::prepare(archive);
        FomodEngine engine;
        if (!prep.ok || prep.fomodConfigPath.isEmpty() || !engine.load(prep.fomodConfigPath)) {
            mod->fomodStatus = "needs-rerun"; ++sum.needsRerun; dirty = true; continue;
        }
        const FomodModule& module = engine.module();

        if (classifyModule(module) == FomodClass::FlagDriven) {
            // Flag-driven: not reconstructable by file-diff. Flag, don't guess.
            mod->fomodStatus = "needs-rerun"; ++sum.needsRerun; dirty = true; continue;
        }

        // Direct-file: build the present-file model from the staged Data tree.
        const QString modData = childCI(stagingRoot + "/" + id, "Data");
        QSet<QString> installed;
        QHash<QString, QString> realByLower;
        if (!modData.isEmpty()) {
            QDir base(modData);
            QDirIterator it(modData, QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
            while (it.hasNext()) {
                const QString full = it.next();
                const QString rel = lowerPath(base.relativeFilePath(full));
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

        // Persist as fomod-choices.json in the same shape onInstallMod writes.
        QJsonArray stepsArr;
        for (const ReconstructedStep& rs : rec.steps) {
            QJsonObject so;
            so["step"] = rs.step;
            QJsonArray picks;
            for (const QString& nm : rs.selected) picks.append(nm);
            so["selected"] = picks;
            stepsArr.append(so);
        }
        QJsonObject root;
        root["installer_version"] = "1.0";
        root["installed_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        root["reconstructed"] = true;
        if (rec.ambiguous) root["ambiguous"] = true;
        root["steps"] = stepsArr;
        QDir().mkpath(choicesDir);
        QFile f(choicesDir + "/" + id + ".json");
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));

        mod->hasFomodChoices = true;
        mod->fomodStatus = "reconstructed";
        ++sum.choicesReconstructed;
        dirty = true;
    }

    if (dirty) profile.save();
    return sum;
}

} // namespace solero
