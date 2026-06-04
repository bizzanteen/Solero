#include "FomodCondition.h"
#include <QDomElement>

namespace solero {

FomodCondition FomodCondition::parse(const QDomElement& element) {
    FomodCondition c;
    if (element.isNull()) return c;
    c.m_alwaysTrue = false;

    const QString tag = element.tagName().toLower();
    if (tag == "flagdependency") {
        c.m_isLeaf = true; c.m_leafKind = LeafKind::Flag;
        c.m_key = element.attribute("flag");
        c.m_value = element.attribute("value");
        return c;
    }
    if (tag == "filedependency") {
        c.m_isLeaf = true;
        QString state = element.attribute("state").toLower();
        c.m_leafKind = (state == "inactive") ? LeafKind::FileInactive
                     : (state == "missing")  ? LeafKind::FileMissing
                                              : LeafKind::FileActive;
        c.m_key = element.attribute("file");
        return c;
    }

    c.m_op = (element.attribute("operator").toLower() == "or") ? Op::Or : Op::And;
    for (QDomElement child = element.firstChildElement(); !child.isNull();
         child = child.nextSiblingElement()) {
        QString ct = child.tagName().toLower();
        if (ct == "dependencies" || ct == "flagdependency" || ct == "filedependency")
            c.m_children.append(parse(child));
    }
    if (c.m_children.isEmpty() && !c.m_isLeaf) c.m_alwaysTrue = true;
    return c;
}

bool FomodCondition::evalLeaf(const QHash<QString, QString>& flags,
                              const FilePresent& filePresent) const {
    switch (m_leafKind) {
        case LeafKind::Flag:         return flags.value(m_key) == m_value;
        case LeafKind::FileActive:   return filePresent(m_key);
        // Limitation: at wizard time Solero only knows present-vs-absent, not
        // active-vs-inactive. We model "Active" == present. "Inactive" means
        // "present but not active", which we cannot observe, so it never
        // matches (returns false) rather than aliasing Active. "Missing" is the
        // logical negation of present.
        case LeafKind::FileInactive: return false;
        case LeafKind::FileMissing:  return !filePresent(m_key);
        default:                     return true;
    }
}

bool FomodCondition::evaluate(const QHash<QString, QString>& flags,
                              const FilePresent& filePresent) const {
    if (m_alwaysTrue) return true;
    if (m_isLeaf) return evalLeaf(flags, filePresent);
    if (m_children.isEmpty()) return true;
    if (m_op == Op::And) {
        for (const auto& ch : m_children) if (!ch.evaluate(flags, filePresent)) return false;
        return true;
    }
    for (const auto& ch : m_children) if (ch.evaluate(flags, filePresent)) return true;
    return false;
}

} // namespace solero
