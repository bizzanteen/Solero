#pragma once
#include <QString>
#include <QList>
#include <QHash>
#include <functional>

class QDomElement;

namespace solero {

class FomodCondition {
public:
    using FilePresent = std::function<bool(const QString&)>;
    bool evaluate(const QHash<QString, QString>& flags, const FilePresent& filePresent) const;
    static FomodCondition parse(const QDomElement& element);

private:
    enum class Op { And, Or };
    enum class LeafKind { None, Flag, FileActive, FileInactive, FileMissing };
    Op m_op = Op::And;
    bool m_isLeaf = false;
    LeafKind m_leafKind = LeafKind::None;
    QString m_key;
    QString m_value;
    QList<FomodCondition> m_children;
    bool m_alwaysTrue = true;
    bool evalLeaf(const QHash<QString, QString>& flags, const FilePresent& filePresent) const;
};

} // namespace solero
