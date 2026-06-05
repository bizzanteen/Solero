#include "Mo2Importer.h"
#include "core/ProfileManager.h"
#include "core/Profile.h"
#include "core/PluginList.h"
#include <QStringList>
#include <QUuid>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
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

// Stage one mod folder. On copy mode, returns the number of files that FAILED
// to copy (0 == fully OK, including an empty mod). On symlink mode, returns 0
// on success and 1 if the link could not be created.
static int stageModFolder(const QString& srcDir, const QString& modDir, bool symlink) {
    // Contents are Data-relative; place under <modDir>/Data/.
    QString dataDir = modDir + "/Data";
    if (symlink) {
        QDir().mkpath(modDir);
        return QFile::link(srcDir, dataDir) ? 0 : 1; // symlink the whole folder as Data
    }
    QDir().mkpath(dataDir);
    QDirIterator it(srcDir, QDir::Files, QDirIterator::Subdirectories);
    int failed = 0;
    while (it.hasNext()) {
        QString f = it.next();
        QString rel = QDir(srcDir).relativeFilePath(f);
        QString dst = dataDir + "/" + rel;
        QDir().mkpath(QFileInfo(dst).path());
        QFile::remove(dst);
        if (!QFile::copy(f, dst)) ++failed;
    }
    return failed; // empty mod folder is a valid (empty) mod -> 0 failures
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
    for (ModEntry& e : entries) {
        if (e.type == EntryType::Mod) {
            QString src = mo2ModsDir + "/" + e.name;
            if (QDir(src).exists()) {
                copyFailures += stageModFolder(src, stagingRoot + "/" + e.id, symlinkMods);
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

    // Plugins. MO2 stores the full load order in loadorder.txt (one plugin per
    // line, top = first to load) and the active set in plugins.txt. We use
    // loadorder.txt for order and plugins.txt only to decide enabled state.
    // If loadorder.txt is missing, fall back to plugins.txt order.
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
    } else if (pl.isOpen() || havePlugins) {
        // Fallback: no loadorder.txt - use plugins.txt order, '*' = enabled.
        pl.seek(0);
        for (const QString& raw : QString::fromUtf8(pl.readAll()).split('\n')) {
            QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            bool enabled = line.startsWith('*');
            appendPlugin(baseName(line), enabled);
        }
    }

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

} // namespace solero
