#include "ToolStore.h"
#include "core/AppConfig.h"
#include "core/FileUtil.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace solero {

QString ToolStore::defaultPath() { return AppConfig::dataRoot() + "/tools.json"; }

ToolStore::ToolStore(const QString& path) : m_path(path) { load(); }

void ToolStore::add(const Executable& e) { m_tools.append(e); }
void ToolStore::update(const Executable& e) {
    for (auto& t : m_tools) if (t.id == e.id) { t = e; return; }
    m_tools.append(e);
}
void ToolStore::remove(const QString& id) {
    m_tools.removeIf([&](const Executable& t){ return t.id == id; });
}

static QJsonObject toJson(const Executable& e) {
    QJsonObject o;
    o["id"]=e.id; o["name"]=e.name; o["binaryPath"]=e.binaryPath;
    o["workingDir"]=e.workingDir; o["arguments"]=e.arguments;
    o["runtime"]=(e.runtime==RuntimeType::Proton)?"proton":"native";
    o["protonVersion"]=e.protonVersion; o["winePrefix"]=e.winePrefix;
    o["runThroughDeployer"]=e.runThroughDeployer; o["isPrimary"]=e.isPrimary;
    o["isCapturingOutput"]=e.isCapturingOutput; o["outputModId"]=e.outputModId;
    o["iconPath"]=e.iconPath;
    QJsonArray acts;
    for (const auto& a : e.extraActions) {
        QJsonObject ao; ao["label"]=a.label; ao["binaryPath"]=a.binaryPath;
        ao["arguments"]=a.arguments; ao["outputModId"]=a.outputModId; acts.append(ao);
    }
    o["extraActions"]=acts;
    return o;
}
static Executable fromJson(const QJsonObject& o) {
    Executable e;
    e.id=o["id"].toString(); e.name=o["name"].toString(); e.binaryPath=o["binaryPath"].toString();
    e.workingDir=o["workingDir"].toString(); e.arguments=o["arguments"].toString();
    e.runtime=(o["runtime"].toString()=="proton")?RuntimeType::Proton:RuntimeType::Native;
    e.protonVersion=o["protonVersion"].toString(); e.winePrefix=o["winePrefix"].toString();
    e.runThroughDeployer=o["runThroughDeployer"].toBool(false); e.isPrimary=o["isPrimary"].toBool(false);
    e.isCapturingOutput=o["isCapturingOutput"].toBool(false); e.outputModId=o["outputModId"].toString();
    e.iconPath=o["iconPath"].toString();
    for (const auto& v : o["extraActions"].toArray()) {
        auto ao=v.toObject(); ToolAction a;
        a.label=ao["label"].toString(); a.binaryPath=ao["binaryPath"].toString();
        a.arguments=ao["arguments"].toString(); a.outputModId=ao["outputModId"].toString();
        e.extraActions.append(a);
    }
    return e;
}

bool ToolStore::load() {
    m_tools.clear();
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array())
        m_tools.append(fromJson(v.toObject()));
    return true;
}
bool ToolStore::save() const {
    QJsonArray arr;
    for (const auto& t : m_tools) arr.append(toJson(t));
    return atomicWrite(m_path, QJsonDocument(arr).toJson(QJsonDocument::Indented));
}
}
