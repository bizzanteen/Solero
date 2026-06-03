#include "FomodEngine.h"
#include <QFile>
#include <QDomDocument>
#include <QTextStream>

namespace solero {

static GroupType parseGroupType(const QString& s) {
    if (s == "SelectExactlyOne") return GroupType::ExactlyOne;
    if (s == "SelectAtMostOne")  return GroupType::AtMostOne;
    if (s == "SelectAtLeastOne") return GroupType::AtLeastOne;
    if (s == "SelectAll")        return GroupType::All;
    return GroupType::Any;
}
static OptionType parseOptionType(const QString& s) {
    if (s == "Required")      return OptionType::Required;
    if (s == "Recommended")   return OptionType::Recommended;
    if (s == "NotUsable")     return OptionType::NotUsable;
    if (s == "CouldBeUsable") return OptionType::CouldBeUsable;
    return OptionType::Optional;
}

static QList<FomodFile> parseFiles(const QDomElement& filesEl) {
    QList<FomodFile> out;
    for (QDomElement e = filesEl.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
        QString tag = e.tagName().toLower();
        if (tag != "file" && tag != "folder") continue;
        FomodFile f;
        f.source = e.attribute("source");
        f.destination = e.attribute("destination", e.attribute("source"));
        f.isFolder = (tag == "folder");
        f.priority = e.attribute("priority", "0").toInt();
        out.append(f);
    }
    return out;
}

QString FomodEngine::selKey(int step, int group, int opt) {
    return QString("%1/%2/%3").arg(step).arg(group).arg(opt);
}

bool FomodEngine::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QDomDocument doc;
    if (!doc.setContent(&f)) return false;
    QDomElement config = doc.documentElement();
    if (config.tagName().toLower() != "config") return false;

    m_module = FomodModule{};
    m_module.moduleName = config.firstChildElement("moduleName").text();

    QDomElement req = config.firstChildElement("requiredInstallFiles");
    if (!req.isNull()) m_module.requiredFiles = parseFiles(req);

    QDomElement steps = config.firstChildElement("installSteps");
    for (QDomElement s = steps.firstChildElement("installStep"); !s.isNull();
         s = s.nextSiblingElement("installStep")) {
        FomodStep step;
        step.name = s.attribute("name");
        QDomElement vis = s.firstChildElement("visible");
        if (!vis.isNull()) { QString x; QTextStream ts(&x); vis.save(ts, 0); step.visibleConditionXml = x; }

        QDomElement groups = s.firstChildElement("optionalFileGroups");
        for (QDomElement g = groups.firstChildElement("group"); !g.isNull();
             g = g.nextSiblingElement("group")) {
            FomodGroup group;
            group.name = g.attribute("name");
            group.type = parseGroupType(g.attribute("type"));
            QDomElement plugins = g.firstChildElement("plugins");
            for (QDomElement p = plugins.firstChildElement("plugin"); !p.isNull();
                 p = p.nextSiblingElement("plugin")) {
                FomodOption opt;
                opt.name = p.attribute("name");
                opt.description = p.firstChildElement("description").text().trimmed();
                QDomElement img = p.firstChildElement("image");
                if (!img.isNull()) opt.imagePath = img.attribute("path");
                opt.files = parseFiles(p.firstChildElement("files"));
                QDomElement cf = p.firstChildElement("conditionFlags");
                for (QDomElement fl = cf.firstChildElement("flag"); !fl.isNull();
                     fl = fl.nextSiblingElement("flag"))
                    opt.flags.append({ fl.attribute("name"), fl.text() });
                QDomElement td = p.firstChildElement("typeDescriptor");
                QDomElement t = td.firstChildElement("type");
                if (!t.isNull()) opt.baseType = parseOptionType(t.attribute("name"));
                else {
                    QDomElement dt = td.firstChildElement("dependencyType");
                    if (!dt.isNull()) {
                        QString x; QTextStream ts(&x); dt.save(ts, 0); opt.conditionTypeXml = x;
                        QDomElement def = dt.firstChildElement("defaultType");
                        if (!def.isNull()) opt.baseType = parseOptionType(def.attribute("name"));
                    }
                }
                group.options.append(opt);
            }
            step.groups.append(group);
        }
        m_module.steps.append(step);
    }

    QDomElement cond = config.firstChildElement("conditionalFileInstalls");
    if (!cond.isNull()) { QString x; QTextStream ts(&x); cond.save(ts, 0); m_module.conditionalInstallsXml = x; }

    m_module.valid = true;
    return true;
}

QHash<QString, QString> FomodEngine::flagsFor(const Selection& sel) const {
    QHash<QString, QString> flags;
    for (int si = 0; si < m_module.steps.size(); ++si)
        for (int gi = 0; gi < m_module.steps[si].groups.size(); ++gi)
            for (int oi = 0; oi < m_module.steps[si].groups[gi].options.size(); ++oi)
                if (sel.value(selKey(si, gi, oi)))
                    for (const auto& fl : m_module.steps[si].groups[gi].options[oi].flags)
                        flags.insert(fl.name, fl.value);
    return flags;
}

bool FomodEngine::isStepVisible(int stepIndex, const Selection& sel, const FilePresent& present) const {
    if (stepIndex < 0 || stepIndex >= m_module.steps.size()) return false;
    const QString& xml = m_module.steps[stepIndex].visibleConditionXml;
    if (xml.isEmpty()) return true;
    QDomDocument doc; doc.setContent(xml);
    FomodCondition c = FomodCondition::parse(doc.documentElement());
    return c.evaluate(flagsFor(sel), present);
}

QList<FomodFile> FomodEngine::collectFiles(const Selection& sel, const FilePresent& present) const {
    QList<FomodFile> out = m_module.requiredFiles;
    for (int si = 0; si < m_module.steps.size(); ++si)
        for (int gi = 0; gi < m_module.steps[si].groups.size(); ++gi)
            for (int oi = 0; oi < m_module.steps[si].groups[gi].options.size(); ++oi)
                if (sel.value(selKey(si, gi, oi)))
                    out.append(m_module.steps[si].groups[gi].options[oi].files);

    if (!m_module.conditionalInstallsXml.isEmpty()) {
        QHash<QString, QString> flags = flagsFor(sel);
        QDomDocument doc; doc.setContent(m_module.conditionalInstallsXml);
        QDomElement patterns = doc.documentElement().firstChildElement("patterns");
        for (QDomElement pat = patterns.firstChildElement("pattern"); !pat.isNull();
             pat = pat.nextSiblingElement("pattern")) {
            FomodCondition c = FomodCondition::parse(pat.firstChildElement("dependencies"));
            if (c.evaluate(flags, present))
                out.append(parseFiles(pat.firstChildElement("files")));
        }
    }
    return out;
}

} // namespace solero
