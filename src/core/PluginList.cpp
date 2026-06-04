#include "PluginList.h"
#include "FileUtil.h"
#include <QFile>

namespace solero {

void PluginList::append(const PluginEntry& e) { m_plugins.append(e); }

void PluginList::remove(const QString& fn) {
    m_plugins.removeIf([&](const PluginEntry& p){ return p.filename == fn; });
}

void PluginList::move(int from, int to) { m_plugins.move(from, to); }

void PluginList::setEnabled(const QString& fn, bool en) {
    if (auto* p = findByFilename(fn)) p->enabled = en;
}

PluginEntry* PluginList::findByFilename(const QString& fn) {
    for (auto& p : m_plugins) if (p.filename == fn) return &p;
    return nullptr;
}

// plugins.txt format: lines starting with '*' are enabled, others disabled
QString PluginList::toPluginsTxt() const {
    QStringList lines;
    for (const auto& p : m_plugins) {
        lines.append((p.enabled ? "*" : "") + p.filename);
    }
    return lines.join('\n') + '\n';
}

PluginList PluginList::fromPluginsTxt(const QString& txt) {
    PluginList list;
    for (const auto& raw : txt.split('\n', Qt::SkipEmptyParts)) {
        QString line = raw.trimmed();
        if (line.startsWith('#') || line.isEmpty()) continue;
        PluginEntry p;
        p.enabled  = line.startsWith('*');
        p.filename = p.enabled ? line.mid(1) : line;
        p.isMaster = p.filename.endsWith(".esm", Qt::CaseInsensitive);
        p.isLight  = p.filename.endsWith(".esl", Qt::CaseInsensitive);
        list.append(p);
    }
    return list;
}

bool PluginList::saveToFile(const QString& path) const {
    return atomicWrite(path, toPluginsTxt().toUtf8());
}

PluginList PluginList::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return fromPluginsTxt(QString::fromUtf8(f.readAll()));
}

} // namespace solero
