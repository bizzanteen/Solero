#include "ConflictIndex.h"
#include "core/FileUtil.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace solero {

void ConflictIndex::setWinner(const QString& relPath, const QString& modId) {
    m_conflicts[relPath].winner = modId;
    m_reverseBuilt = false; // invalidate reverse index
}

void ConflictIndex::recordConflict(const QString& relPath, const QString& winner, const QString& loser) {
    auto& c = m_conflicts[relPath];
    c.winner = winner;
    c.losers.insert(loser);
    m_reverseBuilt = false; // invalidate reverse index
}

void ConflictIndex::clear() {
    m_conflicts.clear();
    m_winningByMod.clear();
    m_losingByMod.clear();
    m_reverseBuilt = false;
}

QString ConflictIndex::winnerOf(const QString& relPath) const {
    return m_conflicts.value(relPath).winner;
}

QSet<QString> ConflictIndex::losersOf(const QString& relPath) const {
    return m_conflicts.value(relPath).losers;
}

bool ConflictIndex::hasConflict(const QString& relPath) const {
    return !m_conflicts.value(relPath).losers.isEmpty();
}

void ConflictIndex::buildReverse() const {
    m_winningByMod.clear();
    m_losingByMod.clear();
    for (auto it = m_conflicts.cbegin(); it != m_conflicts.cend(); ++it) {
        const auto& c = it.value();
        if (c.losers.isEmpty()) continue; // not a real conflict
        m_winningByMod[c.winner].append(it.key());
        for (const auto& loser : c.losers)
            m_losingByMod[loser].append(it.key());
    }
    m_reverseBuilt = true;
}

QStringList ConflictIndex::winningFilesOf(const QString& modId) const {
    if (!m_reverseBuilt) buildReverse();
    return m_winningByMod.value(modId);
}

QStringList ConflictIndex::losingFilesOf(const QString& modId) const {
    if (!m_reverseBuilt) buildReverse();
    return m_losingByMod.value(modId);
}

QStringList ConflictIndex::conflictedPaths() const {
    QStringList result;
    for (auto it = m_conflicts.cbegin(); it != m_conflicts.cend(); ++it)
        if (!it.value().losers.isEmpty())
            result.append(it.key());
    return result;
}

bool ConflictIndex::saveToFile(const QString& path) const {
    QJsonObject root;
    for (auto it = m_conflicts.cbegin(); it != m_conflicts.cend(); ++it) {
        QJsonObject entry;
        entry["winner"] = it.value().winner;
        QJsonArray losers;
        for (const auto& l : it.value().losers) losers.append(l);
        entry["losers"] = losers;
        root.insert(it.key(), entry);
    }
    return atomicWrite(path, QJsonDocument(root).toJson(QJsonDocument::Indented));
}

ConflictIndex ConflictIndex::loadFromFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    ConflictIndex idx;
    auto root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        auto entry = it.value().toObject();
        idx.setWinner(it.key(), entry["winner"].toString());
        for (const auto& v : entry["losers"].toArray())
            idx.recordConflict(it.key(), entry["winner"].toString(), v.toString());
    }
    return idx;
}

} // namespace solero
