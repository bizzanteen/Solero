#include "FomodScanner.h"
#include "FomodEngine.h"
#include <QVector>

namespace solero {

namespace {

QString lowerPath(QString p) { p.replace('\\', '/'); return p.toLower(); }

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

} // namespace solero
