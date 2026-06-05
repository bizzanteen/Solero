#include "PluginList.h"
#include "FileUtil.h"
#include <QFile>
#include <algorithm>

namespace solero {

int pluginBand(const PluginEntry& p) {
    if (p.isLight) return 1;
    if (p.isMaster) return 0;
    return 2;
}

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

QPair<int,int> PluginList::allowedDropRange(int src) const {
    const int n = m_plugins.size();
    if (src < 0 || src >= n) return {1, 0}; // invalid (lo > hi)
    // Official/vanilla plugins are locked in place: no destination is valid, so the
    // view shows a "no-drop" cursor and the row snaps back if the user drags one.
    if (m_plugins.at(src).isOfficial) return {1, 0};

    const PluginEntry& dragged = m_plugins.at(src);

    // Build the "other" list (everything except `src`) so constraint indices are
    // already in post-removal coordinates - directly comparable to QList::move's
    // destination index. `to == k` means the dragged row lands before others[k]
    // (or at the end when k == others.size()).
    QList<const PluginEntry*> others;
    others.reserve(n - 1);
    for (int i = 0; i < n; ++i)
        if (i != src) others.append(&m_plugins.at(i));
    const int m = others.size(); // == n - 1

    int lo = 0;
    int hi = m; // valid destination indices are [0, m]

    // 1. Locked/official block stays on top: a non-official plugin can't drop
    // above the official block. (Officials never reach here - flags() blocks the
    // drag - but clamp defensively.) lo must be >= number of officials.
    int officialCount = 0;
    for (const auto* p : others) if (p->isOfficial) ++officialCount;
    if (!dragged.isOfficial) lo = std::max(lo, officialCount);

    // 2. Band order: the dragged plugin must stay within the contiguous region of
    // its own band (master=0 < light=1 < esp=2). In post-removal coordinates the
    // valid destinations span [firstOfBand, lastOfBand + 1].
    const int band = pluginBand(dragged);
    int firstOfBand = -1, lastOfBand = -1;
    for (int i = 0; i < m; ++i) {
        if (pluginBand(*others[i]) == band) {
            if (firstOfBand < 0) firstOfBand = i;
            lastOfBand = i;
        }
    }
    if (firstOfBand >= 0) {
        lo = std::max(lo, firstOfBand);
        hi = std::min(hi, lastOfBand + 1);
    }

    // 3. Master/dependency order: load after every plugin listed in our masters,
    // and before any plugin that lists US as one of its masters.
    auto eqCI = [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) == 0;
    };
    for (int i = 0; i < m; ++i) {
        const PluginEntry& other = *others[i];
        // `other` is a master of dragged -> dragged must land after others[i].
        for (const QString& mn : dragged.masters)
            if (eqCI(mn, other.filename)) { lo = std::max(lo, i + 1); break; }
        // dragged is a master of `other` -> dragged must land before others[i].
        for (const QString& mn : other.masters)
            if (eqCI(mn, dragged.filename)) { hi = std::min(hi, i); break; }
    }

    return {lo, hi};
}

bool PluginList::isValidMove(int src, int to) const {
    auto [lo, hi] = allowedDropRange(src);
    return lo <= hi && to >= lo && to <= hi;
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

// loadorder.txt: full load order, every plugin, no '*' prefix.
bool PluginList::saveLoadOrderToFile(const QString& path) const {
    QStringList lines;
    for (const auto& p : m_plugins) lines.append(p.filename);
    QString txt = lines.join('\n') + '\n';
    return atomicWrite(path, txt.toUtf8());
}

PluginList PluginList::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return fromPluginsTxt(QString::fromUtf8(f.readAll()));
}

} // namespace solero
