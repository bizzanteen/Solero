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

static bool stageModFolder(const QString& srcDir, const QString& modDir, bool symlink) {
    // Contents are Data-relative; place under <modDir>/Data/.
    QString dataDir = modDir + "/Data";
    if (symlink) {
        QDir().mkpath(modDir);
        return QFile::link(srcDir, dataDir); // symlink the whole folder as Data
    }
    QDir().mkpath(dataDir);
    QDirIterator it(srcDir, QDir::Files, QDirIterator::Subdirectories);
    bool any = false;
    while (it.hasNext()) {
        QString f = it.next();
        QString rel = QDir(srcDir).relativeFilePath(f);
        QString dst = dataDir + "/" + rel;
        QDir().mkpath(QFileInfo(dst).path());
        QFile::remove(dst);
        if (QFile::copy(f, dst)) any = true;
    }
    return any || true; // empty mod folder is still a valid (empty) mod
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
    for (const ModEntry& e : entries) {
        if (e.type == EntryType::Mod) {
            QString src = mo2ModsDir + "/" + e.name;
            if (QDir(src).exists()) {
                stageModFolder(src, stagingRoot + "/" + e.id, symlinkMods);
                ++staged;
            }
        }
        p->modList().append(e);
    }

    // plugins.txt: MO2 marks active plugins with '*'; import as enabled.
    QFile pl(mo2ProfileDir + "/plugins.txt");
    if (pl.open(QIODevice::ReadOnly)) {
        for (const QString& raw : QString::fromUtf8(pl.readAll()).split('\n')) {
            QString line = raw.trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            PluginEntry pe;
            pe.enabled = line.startsWith('*');
            pe.filename = pe.enabled ? line.mid(1) : line;
            pe.isMaster = pe.filename.endsWith(".esm", Qt::CaseInsensitive);
            pe.isLight  = pe.filename.endsWith(".esl", Qt::CaseInsensitive);
            p->pluginList().append(pe);
        }
    }

    p->save();
    r.success = true;
    r.profileName = newProfileName;
    r.modsStaged = staged;
    return r;
}

} // namespace solero
