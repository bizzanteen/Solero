#include "IniFile.h"
#include "core/FileUtil.h"
#include <QFile>

namespace solero {

// Returns the section name if `line` is a "[Section]" header, else a null QString.
static QString sectionHeaderName(const QString& line) {
    QString t = line.trimmed();
    if (t.size() >= 2 && t.startsWith('[') && t.endsWith(']'))
        return t.mid(1, t.size() - 2).trimmed();
    return {};
}

// Is this a comment or blank line (ignored for lookups, preserved verbatim)?
static bool isCommentOrBlank(const QString& line) {
    QString t = line.trimmed();
    return t.isEmpty() || t.startsWith(';') || t.startsWith('#');
}

// If `line` is a "key=value" line, returns the trimmed key; else null QString.
static QString lineKey(const QString& line) {
    if (isCommentOrBlank(line)) return {};
    int eq = line.indexOf('=');
    if (eq < 0) return {};
    return line.left(eq).trimmed();
}

bool IniFile::load(const QString& path) {
    m_lines.clear();
    m_dirty = false;
    if (path.isEmpty()) return true;
    QFile f(path);
    if (!f.exists()) return true;          // missing file = empty doc
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray raw = f.readAll();
    f.close();
    QString text = QString::fromUtf8(raw);
    if (text.isEmpty()) return true;       // empty file = empty doc
    // Split preserving content; strip a single trailing newline so we don't
    // create a spurious empty final element (re-added on save()).
    if (text.endsWith('\n')) text.chop(1);
    if (text.endsWith('\r')) text.chop(1);
    m_lines = text.split('\n');
    // Normalise away trailing \r from CRLF files (kept out of stored lines).
    for (QString& l : m_lines)
        if (l.endsWith('\r')) l.chop(1);
    return true;
}

int IniFile::findSection(const QString& section) const {
    for (int i = 0; i < m_lines.size(); ++i) {
        QString name = sectionHeaderName(m_lines.at(i));
        if (!name.isNull() && name.compare(section, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

int IniFile::findKeyInSection(int sectionLine, const QString& key) const {
    for (int i = sectionLine + 1; i < m_lines.size(); ++i) {
        if (!sectionHeaderName(m_lines.at(i)).isNull()) break; // next section
        QString k = lineKey(m_lines.at(i));
        if (!k.isNull() && k.compare(key, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

bool IniFile::has(const QString& section, const QString& key) const {
    int s = findSection(section);
    if (s < 0) return false;
    return findKeyInSection(s, key) >= 0;
}

QString IniFile::value(const QString& section, const QString& key) const {
    int s = findSection(section);
    if (s < 0) return {};
    int k = findKeyInSection(s, key);
    if (k < 0) return {};
    const QString& line = m_lines.at(k);
    int eq = line.indexOf('=');
    return line.mid(eq + 1).trimmed();
}

void IniFile::setValue(const QString& section, const QString& key, const QString& value) {
    int s = findSection(section);
    if (s >= 0) {
        int k = findKeyInSection(s, key);
        if (k >= 0) {
            // Replace only this line, preserving the existing key spelling/case.
            const QString& line = m_lines.at(k);
            int eq = line.indexOf('=');
            QString existingKey = line.left(eq).trimmed();
            if (line.mid(eq + 1).trimmed() == value) return; // no change
            m_lines[k] = existingKey + "=" + value;
            m_dirty = true;
            return;
        }
        // Section exists but key doesn't: insert after the last key line of it.
        int insertAt = s + 1;
        for (int i = s + 1; i < m_lines.size(); ++i) {
            if (!sectionHeaderName(m_lines.at(i)).isNull()) break; // next section
            if (!lineKey(m_lines.at(i)).isNull()) insertAt = i + 1;
        }
        m_lines.insert(insertAt, key + "=" + value);
        m_dirty = true;
        return;
    }
    // Section doesn't exist: append blank + [section] + key=value at end.
    if (!m_lines.isEmpty()) m_lines.append(QString());
    m_lines.append("[" + section + "]");
    m_lines.append(key + "=" + value);
    m_dirty = true;
}

bool IniFile::save(const QString& path) const {
    QString text = m_lines.join('\n');
    if (!text.endsWith('\n')) text.append('\n');
    return atomicWrite(path, text.toUtf8());
}

} // namespace solero
