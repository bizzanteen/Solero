#pragma once
#include <QString>
#include <QStringList>

namespace solero {

// A line-preserving INI editor. Loads an INI into raw lines and edits in place,
// so comments, blank lines, key spelling/case, and untouched lines survive a
// round-trip byte-for-byte. Unlike QSettings, keys with spaces (e.g.
// "iSize W") are never percent-encoded.
class IniFile {
public:
    // Read lines from `path`. A missing/empty file is treated as an empty doc
    // and still returns true.
    bool load(const QString& path);

    // Exact-section, case-insensitive-key lookup. Returns the trimmed value
    // after the first '=', or "" if the key/section is absent.
    QString value(const QString& section, const QString& key) const;
    bool has(const QString& section, const QString& key) const;

    // Set a key in place (preserving its existing spelling/case if present),
    // creating the key and/or section as needed. Sets dirty() only if the
    // stored value actually changes.
    void setValue(const QString& section, const QString& key, const QString& value);

    // Remove a key line if present (exact section, case-insensitive key). Sets
    // dirty() only if a line was actually removed. The section header is left in
    // place even if it becomes empty.
    void remove(const QString& section, const QString& key);

    // Write via atomicWrite, ensuring a trailing newline.
    bool save(const QString& path) const;

    bool dirty() const { return m_dirty; }

private:
    // Index of the line opening `section` (the "[Section]" line), or -1.
    int findSection(const QString& section) const;
    // Within the section starting at sectionLine, find the line index of `key`,
    // or -1. sectionLine may be -1 to mean "before any section header".
    int findKeyInSection(int sectionLine, const QString& key) const;

    QStringList m_lines;
    bool m_dirty = false;
};

} // namespace solero
