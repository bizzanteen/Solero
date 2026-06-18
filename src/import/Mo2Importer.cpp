#include "Mo2Importer.h"
#include "core/ProfileManager.h"
#include "core/Profile.h"
#include "core/PluginList.h"
#include "core/VersionUtil.h"
#include "core/StagingFolder.h"
#include <QStringList>
#include <QUuid>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QHash>
#include <QSettings>
#include <QRegularExpression>
#include <cctype>

namespace solero {

QList<ModEntry> Mo2Importer::parseModlist(const QString& modlistTxt) {
    QList<ModEntry> topToBottom; // as read (top = highest priority)
    for (const QString& raw : modlistTxt.split('\n')) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        QChar prefix = line.at(0);
        QString name = line.mid(1);
        if (prefix == '*') continue; // unmanaged/MO2-internal
        if (prefix != '+' && prefix != '-') continue;

        ModEntry e;
        if (name.endsWith("_separator")) {
            e.type = EntryType::Separator;
            e.name = name.left(name.size() - QString("_separator").size());
            e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            e.color = "#555555";
            e.enabled = true;
        } else {
            e.type = EntryType::Mod;
            e.name = name;
            e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            e.enabled = (prefix == '+');
        }
        topToBottom.append(e);
    }
    // Reverse into Solero order (index 0 = lowest priority).
    QList<ModEntry> out;
    for (int i = topToBottom.size() - 1; i >= 0; --i) out.append(topToBottom[i]);
    return out;
}

// Normalize a ModOrganizer.ini binary/path value into a native absolute path:
// MO2 (under Proton) stores Windows-style values like
//   Z:/var/home/.../tools/xEdit/SSEEdit.exe   or
//   Z:\\var\\home\\...\\StockGame\\skse64_loader.exe
// Strip a leading Wine drive letter ("Z:") and convert backslashes to forward
// slashes so the result is a Unix path. Leaves an already-Unix path untouched.
static QString normalizeWinePath(QString v) {
    v = v.trimmed();
    v.replace('\\', '/');
    // Strip a leading single-letter Wine drive ("Z:/..." -> "/...").
    if (v.size() >= 2 && v.at(1) == ':' && v.at(0).isLetter())
        v = v.mid(2);
    return v;
}

// Resolve a normalized ModOrganizer.ini binary path to where it will live after
// import. The ini's paths point into the original instance dir; rebase any path
// that sits under that original prefix onto `instanceDir` (the staged/installed
// instance). The original prefix is inferred as the leading portion of the path
// that ends just before a known instance-relative segment (tools/, mods/,
// StockGame/, Stock Game/, Game Root/, Root/, profiles/, overwrite/,
// downloads/). If nothing matches, return the path unchanged.
static QString rebaseInstanceBinary(const QString& normPath, const QString& instanceDir) {
    if (instanceDir.isEmpty()) return normPath;
    static const char* kSegs[] = {
        "/tools/", "/mods/", "/StockGame/", "/Stock Game/", "/Game Root/",
        "/Root/", "/profiles/", "/overwrite/", "/downloads/",
    };
    for (const char* seg : kSegs) {
        const int at = normPath.indexOf(QLatin1String(seg), 0, Qt::CaseInsensitive);
        if (at >= 0) {
            // Keep the segment + remainder; graft onto the real instance dir.
            const QString rel = normPath.mid(at); // starts with "/<seg>…"
            return QDir::cleanPath(instanceDir + rel);
        }
    }
    return normPath;
}

QList<ImportedTool> Mo2Importer::parseCustomExecutables(const QString& iniContent,
                                                        const QString& instanceDir) {
    // Collect title/binary/arguments per numeric index within the
    // [customExecutables] section. MO2 writes keys like "1\title=", "1\binary=",
    // "1\arguments=". We parse the section linearly (QSettings would mangle the
    // backslash-laden Windows paths), preserving the numeric index ORDER.
    struct Raw { QString title, binary, args; };
    QHash<int, Raw> byIndex;
    QList<int> order; // first-seen numeric indices, in file order

    bool inSection = false;
    for (const QString& lineRaw : iniContent.split('\n')) {
        QString line = lineRaw;
        // Strip a trailing CR (CRLF files) without touching interior content.
        if (line.endsWith('\r')) line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
            inSection = (trimmed.compare("[customExecutables]", Qt::CaseInsensitive) == 0);
            continue;
        }
        if (!inSection || trimmed.isEmpty()) continue;

        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1); // keep value verbatim (paths/quotes)

        const int bs = key.indexOf('\\');
        if (bs < 0) continue; // e.g. "size=13" - not an indexed entry
        bool ok = false;
        const int idx = key.left(bs).toInt(&ok);
        if (!ok) continue;
        const QString field = key.mid(bs + 1).trimmed();

        if (!byIndex.contains(idx)) { byIndex.insert(idx, Raw{}); order << idx; }
        Raw& r = byIndex[idx];
        if (field.compare("title", Qt::CaseInsensitive) == 0)          r.title = val.trimmed();
        else if (field.compare("binary", Qt::CaseInsensitive) == 0)    r.binary = val.trimmed();
        else if (field.compare("arguments", Qt::CaseInsensitive) == 0) r.args = val.trimmed();
    }

    QList<ImportedTool> out;
    for (int idx : order) {
        const Raw& r = byIndex.value(idx);
        if (r.title.isEmpty() || r.binary.isEmpty()) continue;
        ImportedTool t;
        t.name = r.title;
        t.binary = rebaseInstanceBinary(normalizeWinePath(r.binary), instanceDir);
        t.args = r.args;
        out << t;
    }
    return out;
}

// A game-root file is one that the game loads from the folder next to
// SkyrimSE.exe rather than from Data/ - primarily native binaries: SKSE's
// skse64_loader.exe, ENB/ASI DLLs, d3dx9_42.dll (Engine Fixes), etc. We
// classify a top-level file as game-root if its suffix is one of these.
static bool isRootFile(const QString& name) {
    const QString suffix = QFileInfo(name).suffix().toLower();
    return suffix == "dll" || suffix == "exe" || suffix == "asi" || suffix == "bin";
}

// Link or copy a single top-level entry (file or directory) of a mod into the
// staging tree. `src` is the absolute source path; `dst` the absolute target.
// In symlink mode we create a symlink (the deployer follows symlinks, including
// for directories); in copy mode we recursively copy files. Returns the number
// of failures (0 == OK).
static int placeEntry(const QString& src, const QString& dst, bool symlink) {
    QDir().mkpath(QFileInfo(dst).path());
    if (symlink) {
        QFile::remove(dst);
        return QFile::link(src, dst) ? 0 : 1;
    }
    QFileInfo si(src);
    if (si.isDir()) {
        QDir().mkpath(dst);
        QDirIterator it(src, QDir::Files, QDirIterator::Subdirectories);
        int failed = 0;
        while (it.hasNext()) {
            QString f = it.next();
            QString rel = QDir(src).relativeFilePath(f);
            QString d = dst + "/" + rel;
            QDir().mkpath(QFileInfo(d).path());
            QFile::remove(d);
            if (!QFile::copy(f, d)) ++failed;
        }
        return failed;
    }
    QFile::remove(dst);
    return QFile::copy(src, dst) ? 0 : 1;
}

// Stage one mod folder into <destUuidDir>, classifying each top-LEVEL entry as
// game-root vs Data/ content so binaries that must sit next to SkyrimSE.exe
// (SKSE loader, ENB/ASI DLLs, d3dx9_42.dll) land at the staging root while
// normal mod content (meshes/, textures/, *.esp, *.bsa, …) lands under Data/.
//
// Classification, per top-level entry (skipping meta.ini, *.modgroups, dotfiles):
//   1. dir named "Root" (case-insensitive)  -> Root Builder: each child -> root.
//   2. else if mod has a top-level "Data" subdir -> mod is game-root-relative:
//        place the entry AS-IS at the staging root (its own Data subdir maps to
//        <game>/Data; root *.dll/*.exe map to game root).
//   3. else if entry is a file with a root suffix (dll/exe/asi/bin) -> root.
//   4. else (normal Data-relative content) -> under Data/.
//
// On copy mode returns the number of files that FAILED to copy (0 == fully OK,
// including an empty mod). On symlink mode returns the number of links that
// could not be created.
static int stageModClassified(const QString& srcDir, const QString& destUuidDir, bool symlink) {
    QDir().mkpath(destUuidDir);

    // Detect a top-level "Data" subdir (case-insensitive) - marks the mod as
    // game-root-relative (rule 2).
    bool hasDataSubdir = false;
    const QFileInfoList topInfos =
        QDir(srcDir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : topInfos) {
        if (fi.isDir() && fi.fileName().compare("Data", Qt::CaseInsensitive) == 0) {
            hasDataSubdir = true;
            break;
        }
    }

    int failed = 0;
    for (const QFileInfo& fi : topInfos) {
        const QString name = fi.fileName();
        // Skip MO2/Solero metadata and dotfiles (never game content).
        if (name.startsWith('.')) continue;
        if (name.compare("meta.ini", Qt::CaseInsensitive) == 0) continue;
        if (name.endsWith(".modgroups", Qt::CaseInsensitive)) continue;

        const QString src = srcDir + "/" + name;

        // Rule 1: a "Root" directory holds game-root content (Root Builder).
        if (fi.isDir() && name.compare("Root", Qt::CaseInsensitive) == 0) {
            const QFileInfoList children =
                QDir(src).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
            for (const QFileInfo& ci : children) {
                if (ci.fileName().startsWith('.')) continue;
                failed += placeEntry(src + "/" + ci.fileName(),
                                     destUuidDir + "/" + ci.fileName(), symlink);
            }
            continue;
        }

        // Rule 2: game-root-relative mod -> place entry at staging root as-is.
        if (hasDataSubdir) {
            failed += placeEntry(src, destUuidDir + "/" + name, symlink);
            continue;
        }

        // Rule 3: a bare root-suffixed file -> game root.
        if (fi.isFile() && isRootFile(name)) {
            failed += placeEntry(src, destUuidDir + "/" + name, symlink);
            continue;
        }

        // Rule 4: normal Data-relative content -> under Data/.
        failed += placeEntry(src, destUuidDir + "/Data/" + name, symlink);
    }
    return failed; // empty mod folder is a valid (empty) mod -> 0 failures
}

// A Wabbajack-built list ships a separator named after the modlist itself
// (e.g. "A Dragonborn's Fate 10.2.26" == the list title + version). That header
// is redundant with the Solero profile name and just clutters the list, so we
// skip any separator whose name starts with the modlist title. Guard against an
// empty listTitle (would otherwise match every separator).
static bool isListTitleSeparator(const QString& sepName, const QString& listTitle) {
    if (listTitle.trimmed().isEmpty()) return false;
    return sepName.trimmed().startsWith(listTitle.trimmed(), Qt::CaseInsensitive);
}

// MO2 keeps a few non-content folders inside its mods dir (e.g. "ModGroups",
// which holds *.modgroups separator-group metadata, and sometimes "Backup").
// These are listed in modlist.txt like real mods but are not game content, so
// they must never become Solero mod entries. Returns true for such artifacts.
static bool isMo2Artifact(const QString& modName) {
    return modName.compare("ModGroups", Qt::CaseInsensitive) == 0
        || modName.compare("Backup",    Qt::CaseInsensitive) == 0;
}

// Read Nexus identity + version from a mod's <modsDir>/<ModName>/meta.ini.
// MO2 writes e.g.:
//   [General]
//   modid=149975
//   version=1.7.1.0
//   repository=Nexus
// Sets e.nexusModId only when modid is a positive integer and repository is
// empty or "Nexus". Sets e.version from the General/version key. Robust to a
// missing file (most mods have one; some won't).
static void applyModMeta(const QString& modDir, ModEntry& e) {
    const QString metaIni = modDir + "/meta.ini";
    if (!QFileInfo::exists(metaIni)) return;
    // QSettings folds the INI's [General] section into the top level, so read
    // the keys without a "General/" prefix (mirrors selected_profile handling).
    QSettings s(metaIni, QSettings::IniFormat);
    const QString version    = s.value("version").toString().trimmed();
    const QString repository  = s.value("repository").toString().trimmed();
    const QString modIdStr    = s.value("modid").toString().trimmed();

    if (!version.isEmpty()) e.version = normalizeVersion(version);

    const bool repoOk = repository.isEmpty()
                     || repository.compare("Nexus", Qt::CaseInsensitive) == 0;
    if (repoOk) {
        bool ok = false;
        const int modId = modIdStr.toInt(&ok);
        if (ok && modId > 0) e.nexusModId = QString::number(modId);
    }
}

// Assign a unique, name-based staging folder to `e` (from its current name),
// reserving it in `taken` (lowercased) so siblings can't collide. Returns the
// absolute on-disk staging path the mod's files should be written to. Use this
// before staging so import produces name-based folders directly (no UUID dirs
// left for migrateStagingFolders() to rename on the next load).
static QString assignStagingFolder(ModEntry& e, const QString& stagingRoot,
                                   QSet<QString>& taken) {
    e.stagingFolder =
        uniqueStagingFolder(sanitizeStagingFolder(e.name), taken);
    taken.insert(e.stagingFolder.toLower());
    return stagingRoot + "/" + e.stagingFolder;
}

// Stage a single MO2 mod by name into stagingRoot, returning a ModEntry that
// references the staged copy (fresh UUID + name + name-based staging folder). On
// copy mode, copyFailures is incremented by the number of files that failed to
// copy. The source folder is assumed to exist (callers check QDir(src).exists()
// first). `taken` carries reserved (lowercased) folder names across the import.
static ModEntry stageMod(const QString& modName, const QString& mo2ModsDir,
                         const QString& stagingRoot, bool symlink, int& copyFailures,
                         QSet<QString>& taken) {
    ModEntry e;
    e.type = EntryType::Mod;
    e.name = modName;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString src = mo2ModsDir + "/" + modName;
    applyModMeta(src, e);
    const QString dest = assignStagingFolder(e, stagingRoot, taken);
    copyFailures += stageModClassified(src, dest, symlink);
    return e;
}

// Read a separator colour from <modsDir>/<sepFolder>_separator/meta.ini.
// MO2 stores the colour under [General] color= in one of two forms:
//   1. A Qt-serialised QColor variant:  color=@Variant(\0\0\0\x43\x1\xff\xff...)
//   2. A plain hex/name string:         color=#rrggbb  (older / hand-edited)
// Returns an empty string if no usable colour is found.
static QString readSeparatorColor(const QString& metaIniPath) {
    // NOTE: we do not use QSettings here. QSettings tries to natively decode
    // MO2's `color=@Variant(...)` value as a serialised QVariant, but without
    // QtGui's QColor registered it yields an *invalid* variant - so we'd never
    // see the raw bytes. Instead, read the raw `color=` line ourselves.
    QFile mf(metaIniPath);
    if (!mf.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QString raw;
    bool inGeneral = false;
    for (const QByteArray& lineBytes : mf.readAll().split('\n')) {
        QString line = QString::fromUtf8(lineBytes).trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            inGeneral = (line.compare("[General]", Qt::CaseInsensitive) == 0);
            continue;
        }
        // MO2 keeps `color=` under [General]; accept it anywhere as a fallback.
        if (line.startsWith("color=", Qt::CaseInsensitive)) {
            QString val = line.mid(QString("color=").size()).trimmed();
            if (inGeneral) { raw = val; break; }     // prefer the [General] one
            if (raw.isEmpty()) raw = val;            // remember non-section match
        }
    }
    if (raw.isEmpty()) return QString();

    // Form 2: plain hex string, e.g. "#rrggbb" (older / hand-edited metas).
    if (raw.startsWith('#')) {
        static const QRegularExpression hex("^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$");
        if (hex.match(raw).hasMatch())
            return raw.left(7).toLower();   // drop optional alpha, normalise
        return QString();
    }

    // Form 1: a Qt-serialised QColor variant. MO2 writes:
    //   color=@Variant(\0\0\0\x43\x1<spec:1><a:2><r:2><g:2><b:2><pad:2>)
    // where 0x43 == QMetaType::QColor and each 16-bit channel duplicates its
    // 8-bit value (e.g. 0x6d6d -> 0x6d). Decode the C-escaped byte payload and
    // pull out r/g/b without needing QtGui's QColor.
    if (!raw.startsWith(QStringLiteral("@Variant"), Qt::CaseInsensitive)) return QString();
    int open = raw.indexOf('(');
    int close = raw.lastIndexOf(')');
    if (open < 0 || close <= open) return QString();
    const QString inner = raw.mid(open + 1, close - open - 1);

    QByteArray bytes;
    for (int i = 0; i < inner.size(); ++i) {
        QChar ch = inner.at(i);
        if (ch != '\\') { bytes.append(static_cast<char>(ch.toLatin1())); continue; }
        if (++i >= inner.size()) break;
        QChar e = inner.at(i);
        if (e == 'x') {                       // \xNN hex escape (1-2 hex digits)
            QString h;
            while (i + 1 < inner.size() && h.size() < 2 &&
                   std::isxdigit(static_cast<unsigned char>(inner.at(i + 1).toLatin1()))) {
                h.append(inner.at(i + 1)); ++i;
            }
            if (!h.isEmpty()) bytes.append(static_cast<char>(h.toInt(nullptr, 16)));
        } else if (e == '0') { bytes.append('\0'); }
        else if (e == 'n') { bytes.append('\n'); }
        else if (e == 'r') { bytes.append('\r'); }
        else if (e == 't') { bytes.append('\t'); }
        else { bytes.append(static_cast<char>(e.toLatin1())); }
    }

    // Layout: [type:4][spec:1][a:2][r:2][g:2][b:2]... - take the high byte of
    // each 16-bit channel. Verify the type id is QColor (0x43) first.
    if (bytes.size() < 13) return QString();
    const auto u8 = [&](int i) { return static_cast<unsigned char>(bytes.at(i)); };
    const quint32 typeId = (u8(0) << 24) | (u8(1) << 16) | (u8(2) << 8) | u8(3);
    if (typeId != 0x43) return QString();
    // offset 5 = alpha hi/lo, 7 = red, 9 = green, 11 = blue (big-endian 16-bit).
    const int r = u8(7), g = u8(9), b = u8(11);
    return QString::asprintf("#%02x%02x%02x", r, g, b);
}

// Apply an MO2 profile's plugin load order + enabled state onto a Solero
// profile. MO2 stores the full load order in loadorder.txt (one plugin per
// line, top = first to load) and the active set in plugins.txt. We use
// loadorder.txt for order and plugins.txt only to decide enabled state. If
// loadorder.txt is missing, fall back to plugins.txt order ('*' = enabled).
static void applyPlugins(const QString& mo2ProfileDir, Profile* p) {
    auto baseName = [](QString line) -> QString {
        line = line.trimmed();
        if (line.startsWith('*')) line = line.mid(1); // MO2 active-prefix form
        return line;
    };

    // Build the enabled set from plugins.txt. A plugin is enabled if it appears
    // there - handle both the '*'-prefixed form and the plain-list form.
    QSet<QString> enabledSet;
    bool havePlugins = false;
    QFile pl(mo2ProfileDir + "/plugins.txt");
    if (pl.open(QIODevice::ReadOnly)) {
        havePlugins = true;
        for (const QString& raw : QString::fromUtf8(pl.readAll()).split('\n')) {
            QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            enabledSet.insert(baseName(line).toLower());
        }
    }

    auto appendPlugin = [&](const QString& filename, bool enabled) {
        PluginEntry pe;
        pe.filename = filename;
        pe.enabled  = enabled;
        pe.isMaster = pe.filename.endsWith(".esm", Qt::CaseInsensitive);
        pe.isLight  = pe.filename.endsWith(".esl", Qt::CaseInsensitive);
        p->pluginList().append(pe);
    };

    QFile lo(mo2ProfileDir + "/loadorder.txt");
    if (lo.open(QIODevice::ReadOnly)) {
        // loadorder.txt drives the order; enabled state comes from plugins.txt
        // (or true if plugins.txt is absent, mirroring MO2's "all active").
        for (const QString& raw : QString::fromUtf8(lo.readAll()).split('\n')) {
            QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            const QString name = baseName(line);
            bool enabled = havePlugins ? enabledSet.contains(name.toLower()) : true;
            appendPlugin(name, enabled);
        }
    } else if (havePlugins) {
        // Fallback: no loadorder.txt - use plugins.txt order, '*' = enabled.
        pl.seek(0);
        for (const QString& raw : QString::fromUtf8(pl.readAll()).split('\n')) {
            QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            bool enabled = line.startsWith('*');
            appendPlugin(baseName(line), enabled);
        }
    }
}

// Case-insensitive child lookup: return the full path of the entry under
// `parent` whose name matches `name` (case-insensitively), or empty if none.
// Mirrors the childCI helper used in install/PluginScanner.cpp.
static QString childCI(const QString& parent, const QString& name) {
    QDir d(parent);
    if (!d.exists()) return {};
    for (const QString& e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
        if (e.compare(name, Qt::CaseInsensitive) == 0) return parent + "/" + e;
    return {};
}

// Vanilla / engine files that live in the game root but are not mod-added - they
// belong to the base install (or Proton) and must never be staged as a mod.
static bool isVanillaRootFile(const QString& name) {
    static const char* kBlock[] = {
        "SkyrimSE.exe", "SkyrimSELauncher.exe", "SkyrimVR.exe",
        "steam_api64.dll", "steam_api.dll", "bink2w64.dll", "binkw64.dll",
        "Skyrim.ccc", "installscript.vdf", "steam_appid.txt",
    };
    for (const char* b : kBlock)
        if (name.compare(QLatin1String(b), Qt::CaseInsensitive) == 0) return true;
    return false;
}

// Proton/DXVK/runtime cruft that may sit next to the exe but is regenerated and
// not mod content (e.g. SkyrimSE.dxvk-cache, *.log, steam_emu.ini-style *.vdf).
static bool isRootCruft(const QString& name) {
    const QString lower = name.toLower();
    if (lower.endsWith(".dxvk-cache")) return true;
    static const char* kSuffix[] = { "cache", "conf", "log", "vdf", "dxvk-cache" };
    const QString suffix = QFileInfo(name).suffix().toLower();
    for (const char* s : kSuffix)
        if (suffix == QLatin1String(s)) return true;
    return false;
}

ModEntry Mo2Importer::stageGameRootOverlay(const QString& instanceDir,
                                           const QString& stagingRoot, bool symlink,
                                           QSet<QString>& taken) {
    ModEntry empty; // id stays empty -> "invalid" sentinel

    // Find the overlay folder (case-insensitive) among the known names.
    QString overlayDir;
    for (const char* candidate : { "StockGame", "Stock Game", "Game Root",
                                   "Root", "Stock Game Folder" }) {
        const QString hit = childCI(instanceDir, QLatin1String(candidate));
        if (!hit.isEmpty() && QFileInfo(hit).isDir()) { overlayDir = hit; break; }
    }
    if (overlayDir.isEmpty()) return empty;

    // Enumerate only the top-level FILES (never descend into subdirs like Data/,
    // Creations/, Mods/, _CommonRedist/). Keep anything that isn't vanilla or cruft.
    QStringList keep;
    for (const QFileInfo& fi :
         QDir(overlayDir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        const QString name = fi.fileName();
        if (name.startsWith('.')) continue;
        if (isVanillaRootFile(name)) continue;
        if (isRootCruft(name)) continue;
        keep << name;
    }
    if (keep.isEmpty()) return empty;

    ModEntry e;
    e.type = EntryType::Mod;
    e.name = "Game Root Files";
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.enabled = true;
    e.isOutputMod = false;

    const QString destDir = assignStagingFolder(e, stagingRoot, taken);
    QDir().mkpath(destDir);
    for (const QString& name : keep)
        placeEntry(overlayDir + "/" + name, destDir + "/" + name, symlink);
    return e;
}

Mo2ImportResult Mo2Importer::importProfile(const QString& mo2ProfileDir,
                                           const QString& mo2ModsDir,
                                           const QString& stagingRoot,
                                           ProfileManager& profiles,
                                           const QString& newProfileName,
                                           bool symlinkMods) {
    Mo2ImportResult r;
    QFile ml(mo2ProfileDir + "/modlist.txt");
    if (!ml.open(QIODevice::ReadOnly)) { r.errorMessage = "modlist.txt not found."; return r; }
    QList<ModEntry> entries = parseModlist(QString::fromUtf8(ml.readAll()));

    if (!profiles.createProfile(newProfileName)) {
        r.errorMessage = "Profile '" + newProfileName + "' already exists."; return r;
    }
    Profile* p = profiles.loadProfile(newProfileName);
    if (!p) { r.errorMessage = "Could not create profile."; return r; }

    int staged = 0;
    int copyFailures = 0;
    QSet<QString> taken; // assigned name-based staging folders, kept unique
    for (ModEntry& e : entries) {
        if (e.type == EntryType::Mod) {
            // Skip MO2's non-content artifact folders (e.g. ModGroups).
            if (isMo2Artifact(e.name)) continue;
            QString src = mo2ModsDir + "/" + e.name;
            if (QDir(src).exists()) {
                applyModMeta(src, e);
                copyFailures += stageModClassified(
                    src, assignStagingFolder(e, stagingRoot, taken), symlinkMods);
                ++staged;
            }
        } else if (e.type == EntryType::Separator) {
            // Pull the real separator colour from its meta.ini if present.
            const QString metaIni = mo2ModsDir + "/" + e.name + "_separator/meta.ini";
            const QString col = readSeparatorColor(metaIni);
            if (!col.isEmpty()) e.color = col;
        }
        p->modList().append(e);
    }

    // Capture the game-root overlay (StockGame / "Game Root" / …) - the loose
    // root binaries (d3dx9_42.dll, SKSE loader, ENB dlls) that Wabbajack/Fluorine
    // keep there instead of in mods/. The MO2 instance dir is the mods/ parent.
    {
        const QString instanceDir = QDir(mo2ModsDir + "/..").canonicalPath();
        // Avoid duplicating an already-present "Game Root Files" mod.
        bool already = false;
        for (auto it = p->modList().begin(); it != p->modList().end(); ++it)
            if (it->type == EntryType::Mod &&
                it->name.compare("Game Root Files", Qt::CaseInsensitive) == 0) {
                already = true; break;
            }
        if (!already) {
            ModEntry rootMod = stageGameRootOverlay(instanceDir, stagingRoot, symlinkMods, taken);
            if (!rootMod.id.isEmpty()) {
                p->modList().append(rootMod);
                // Move to index 0 (lowest priority / top of MO2 load order).
                p->modList().move(p->modList().count() - 1, 0);
                ++staged;
            }
        }
    }

    applyPlugins(mo2ProfileDir, p);

    p->save();
    r.success = (copyFailures == 0);
    r.profileName = newProfileName;
    r.modsStaged = staged;
    if (copyFailures > 0) {
        r.errorMessage = QString("%1 file(s) failed to copy during import; "
                                 "the imported profile may be incomplete.").arg(copyFailures);
    }
    return r;
}

Mo2InstanceImportResult Mo2Importer::importInstance(const QString& mo2InstanceDir,
                                                    const QString& stagingRoot,
                                                    ProfileManager& profiles,
                                                    const QString& listTitle,
                                                    bool symlinkMods) {
    Mo2InstanceImportResult r;
    const QString modsDir = mo2InstanceDir + "/mods";
    const QString profilesDir = mo2InstanceDir + "/profiles";

    // Enumerate MO2 profile subdirs (each must carry a modlist.txt).
    QStringList mo2Profiles;
    for (const QString& sub : QDir(profilesDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                          QDir::Name)) {
        if (QFileInfo::exists(profilesDir + "/" + sub + "/modlist.txt"))
            mo2Profiles << sub;
    }
    if (mo2Profiles.isEmpty()) {
        r.errorMessage = "No MO2 profiles (with modlist.txt) found under\n" + profilesDir;
        return r;
    }

    // Parse every profile's modlist.txt once, building the UNION of real mod
    // names referenced across all of them.
    QHash<QString, QList<ModEntry>> parsedByProfile; // mo2 name -> Solero-ordered entries
    QStringList uniqueOrder;                          // first-seen order of unique mod names
    QSet<QString> seen;                               // lowercased names already queued
    for (const QString& mp : mo2Profiles) {
        QFile ml(profilesDir + "/" + mp + "/modlist.txt");
        if (!ml.open(QIODevice::ReadOnly)) continue;
        QList<ModEntry> entries = parseModlist(QString::fromUtf8(ml.readAll()));
        for (const ModEntry& e : entries) {
            if (e.type != EntryType::Mod) continue;
            if (isMo2Artifact(e.name)) continue;                  // skip ModGroups etc.
            const QString key = e.name.toLower();
            if (seen.contains(key)) continue;
            if (!QDir(modsDir + "/" + e.name).exists()) continue; // only real folders
            seen.insert(key);
            uniqueOrder << e.name;
        }
        parsedByProfile.insert(mp, entries);
    }

    // Stage each unique mod EXACTLY once; share id+name across all profiles.
    QHash<QString, ModEntry> sharedMods; // lowercased name -> staged ModEntry
    int copyFailures = 0;
    QSet<QString> taken; // assigned name-based staging folders, kept unique
    for (const QString& name : uniqueOrder) {
        ModEntry staged = stageMod(name, modsDir, stagingRoot, symlinkMods, copyFailures, taken);
        sharedMods.insert(name.toLower(), staged);
    }

    // FIX 2: import the engine's "__Files Requiring Manual Install" folder (the
    // literal name, with leading double underscore) as an extra mod. It holds
    // the game-root binaries the engine couldn't auto-place (skse64_loader.exe,
    // skse64_*.dll, d3dx9_42.dll) plus any Data/ content. Stage it once via the
    // same classifier and reference it (enabled, near the top of the load order)
    // in every created profile, so other mods can override it.
    ModEntry manualInstallMod;
    bool haveManualInstall = false;
    {
        const QString manualDir = mo2InstanceDir + "/__Files Requiring Manual Install";
        QDir md(manualDir);
        if (md.exists() &&
            !md.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
            manualInstallMod.type = EntryType::Mod;
            manualInstallMod.name = "Manual Install Files";
            manualInstallMod.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            manualInstallMod.enabled = true;
            copyFailures += stageModClassified(
                manualDir, assignStagingFolder(manualInstallMod, stagingRoot, taken), symlinkMods);
            haveManualInstall = true;
        }
    }

    // Capture the game-root overlay (StockGame / "Game Root" / …) once and share
    // its id across every created profile (lowest priority, like base/SKSE content).
    const ModEntry gameRootMod =
        stageGameRootOverlay(mo2InstanceDir, stagingRoot, symlinkMods, taken);
    const bool haveGameRoot = !gameRootMod.id.isEmpty();

    r.modsStaged = sharedMods.size() + (haveManualInstall ? 1 : 0)
                 + (haveGameRoot ? 1 : 0);

    // Resolve MO2 selected_profile -> the MO2 profile name we should map primary to.
    QString selectedMo2;
    const QString iniPath = mo2InstanceDir + "/ModOrganizer.ini";
    if (QFileInfo::exists(iniPath)) {
        // Discover the instance's configured tools ([customExecutables]), rebasing
        // each binary onto this (installed) instance dir. QSettings can't be used
        // for this section (its backslash-laden Windows paths get mangled), so the
        // parser reads the raw ini text.
        QFile iniFile(iniPath);
        if (iniFile.open(QIODevice::ReadOnly))
            r.tools = parseCustomExecutables(
                QString::fromUtf8(iniFile.readAll()), mo2InstanceDir);

        QSettings ini(iniPath, QSettings::IniFormat);
        // NOTE: QSettings folds the INI's special [General] section into the
        // top level, so read without a "General/" group prefix (a beginGroup
        // ("General") lookup would return empty for these keys).
        selectedMo2 = ini.value("selected_profile").toString();
        // MO2 sometimes wraps values in @ByteArray(...) / quotes; strip both.
        if (selectedMo2.startsWith("@ByteArray(", Qt::CaseInsensitive)) {
            int open = selectedMo2.indexOf('(');
            int close = selectedMo2.lastIndexOf(')');
            if (open >= 0 && close > open)
                selectedMo2 = selectedMo2.mid(open + 1, close - open - 1);
        }
        selectedMo2.remove('"');
        selectedMo2 = selectedMo2.trimmed();
    }

    // FIX 3: listTitle arrives RAW (unsanitized) so the separator filter below
    // can match the real WJ header (which keeps the title's apostrophe). For
    // profile-name disambiguation we want a filesystem-friendly form, so derive
    // a sanitized variant here (mirrors WabbajackDialog::sanitize).
    auto sanitizeTitle = [](const QString& s) {
        QString out;
        out.reserve(s.size());
        for (QChar c : s) {
            if (c.isLetterOrNumber() || c == ' ' || c == '-' || c == '_') out += c;
            else out += ' ';
        }
        return out.simplified();
    };
    const QString listTitleForName = sanitizeTitle(listTitle);

    // Build a Solero profile per MO2 profile, referencing the shared staged mods.
    QHash<QString, QString> mo2ToSolero; // MO2 profile name -> created Solero name
    for (const QString& mp : mo2Profiles) {
        const QList<ModEntry>& entries = parsedByProfile.value(mp);

        // Disambiguate the Solero profile name against existing profiles.
        QString soleroName = mp;
        const QStringList existing = profiles.profileNames();
        auto taken = [&](const QString& n) { return existing.contains(n); };
        if (taken(soleroName)) {
            soleroName = mp + " (" + listTitleForName + ")";
            QString base = soleroName;
            int n = 2;
            while (taken(soleroName)) {
                soleroName = base + " " + QString::number(n);
                ++n;
            }
        }

        if (!profiles.createProfile(soleroName)) {
            r.errorMessage = "Could not create profile '" + soleroName + "'.";
            r.success = false;
            return r;
        }
        Profile* p = profiles.loadProfile(soleroName);
        if (!p) {
            r.errorMessage = "Could not load created profile '" + soleroName + "'.";
            r.success = false;
            return r;
        }

        // Place the game-root overlay mod at the very top of the load order
        // (lowest priority == index 0 in Solero order), enabled, so the loose
        // root binaries (d3dx9_42.dll, SKSE loader, ENB dlls) are present but any
        // later mod can still override them. Same shared id across all profiles.
        if (haveGameRoot) {
            ModEntry m = gameRootMod;
            m.enabled = true;
            p->modList().append(m);
        }

        // Place the manual-install mod near the top of the load order (lowest
        // priority == index 0 in Solero order), enabled, so its game-root
        // binaries are present but any later mod can still override its content.
        if (haveManualInstall) {
            ModEntry m = manualInstallMod;
            m.enabled = true;
            p->modList().append(m);
        }

        for (const ModEntry& e : entries) {
            if (e.type == EntryType::Separator) {
                // Skip the redundant modlist-name header separator (WJ lists ship
                // one named after the list title/version). Use the RAW listTitle
                // here (FIX 3) so it matches the WJ header's real apostrophe.
                if (isListTitleSeparator(e.name, listTitle)) continue;
                // Separators are per-profile UI entries (fresh UUID, kept from parse).
                ModEntry sep = e;
                const QString metaIni = modsDir + "/" + e.name + "_separator/meta.ini";
                const QString col = readSeparatorColor(metaIni);
                if (!col.isEmpty()) sep.color = col;
                p->modList().append(sep);
            } else {
                if (isMo2Artifact(e.name)) continue;  // never import ModGroups etc.
                // Reference the SHARED staged mod (same id + name); preserve this
                // profile's enabled state. Skip mods not staged (missing folder).
                auto it = sharedMods.constFind(e.name.toLower());
                if (it == sharedMods.constEnd()) continue;
                ModEntry m = it.value();
                m.enabled = e.enabled;
                p->modList().append(m);
            }
        }

        applyPlugins(profilesDir + "/" + mp, p);
        p->save();

        r.profileNames << soleroName;
        mo2ToSolero.insert(mp, soleroName);
    }

    // primaryProfile = the Solero name for MO2 selected_profile, else first created.
    if (!selectedMo2.isEmpty() && mo2ToSolero.contains(selectedMo2))
        r.primaryProfile = mo2ToSolero.value(selectedMo2);
    else
        r.primaryProfile = r.profileNames.isEmpty() ? QString() : r.profileNames.first();

    // Reject a zero-mod import: a partial/failed Wabbajack install can produce a
    // valid MO2 profile layout that references ZERO real mods (the engine staged
    // no mods/ content). Treating that as success would write an orphaned profile
    // whose modlist.json is "[]". Self-clean any profiles this call created (they
    // are exactly the ones in r.profileNames - never pre-existing) so no orphan is
    // left behind.
    if (r.modsStaged == 0) {
        for (const QString& name : r.profileNames) profiles.deleteProfile(name);
        r.profileNames.clear();
        r.primaryProfile.clear();
        r.success = false;
        r.errorMessage = "The install produced no mods to import - the "
                         "modlist install likely failed or was incomplete. "
                         "No profile was created.";
        return r;
    }

    r.success = (copyFailures == 0) && !r.profileNames.isEmpty() && r.modsStaged > 0;
    if (copyFailures > 0) {
        r.errorMessage = QString("%1 file(s) failed to copy during import; "
                                 "the imported profiles may be incomplete.").arg(copyFailures);
    }
    return r;
}

} // namespace solero
