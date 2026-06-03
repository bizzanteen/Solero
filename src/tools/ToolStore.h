#pragma once
#include "core/Types.h"
#include <QString>
#include <QList>
namespace solero {
class ToolStore {
public:
    explicit ToolStore(const QString& path);
    void add(const Executable& e);
    void update(const Executable& e); // by id
    void remove(const QString& id);
    const QList<Executable>& tools() const { return m_tools; }
    bool load();
    bool save() const;
    static QString defaultPath(); // ~/.local/share/solero/tools.json
private:
    QString m_path;
    QList<Executable> m_tools;
};
}
