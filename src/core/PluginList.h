#pragma once
#include "Types.h"
#include <QString>

namespace solero {

class PluginList {
public:
    void append(const PluginEntry& entry);
    void remove(const QString& filename);
    void move(int from, int to);
    void setEnabled(const QString& filename, bool enabled);

    int count() const { return m_plugins.size(); }
    const PluginEntry& at(int index) const { return m_plugins.at(index); }
    PluginEntry* findByFilename(const QString& filename);

    QString toPluginsTxt() const;
    static PluginList fromPluginsTxt(const QString& txt);

    bool saveToFile(const QString& path) const;
    // Writes loadorder.txt: every plugin filename in list order, one per line,
    // with no prefix (full order, active and inactive alike).
    bool saveLoadOrderToFile(const QString& path) const;
    static PluginList loadFromFile(const QString& path);

private:
    QList<PluginEntry> m_plugins;
};

} // namespace solero
