#pragma once
#include "Types.h"
#include <QJsonDocument>

namespace solero {

class ModList {
public:
    void append(const ModEntry& entry);
    void remove(const QString& id);
    void move(int from, int to);
    // Move a contiguous block of `count` entries starting at raw index `from` so
    // that the block's first element lands at raw index `to` (interpreted against
    // the list with the block removed). Used to drag a separator together with
    // every mod in its section.
    void moveSection(int from, int count, int to);
    void setEnabled(const QString& id, bool enabled);
    void update(const QString& id, const ModEntry& updated);

    int count() const { return m_entries.size(); }
    const ModEntry& at(int index) const { return m_entries.at(index); }
    ModEntry* findById(const QString& id);
    const ModEntry* findById(const QString& id) const;

    QJsonDocument toJson() const;
    static ModList fromJson(const QJsonDocument& doc);

    bool saveToFile(const QString& path) const;
    static ModList loadFromFile(const QString& path);

    using const_iterator = QList<ModEntry>::const_iterator;
    const_iterator begin() const { return m_entries.begin(); }
    const_iterator end()   const { return m_entries.end(); }

private:
    QList<ModEntry> m_entries;
    void propagateEnabled(const QString& parentId, bool enabled);
};

} // namespace solero
