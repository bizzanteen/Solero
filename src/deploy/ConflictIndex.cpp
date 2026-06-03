#include "ConflictIndex.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace solero {

void ConflictIndex::setWinner(const QString& relPath, const QString& modId) {
    m_conflicts[relPath].winner = modId;
}

void ConflictIndex::recordConflict(const QString& relPath, const QString& winner, const QString& loser) {
    auto& c = m_conflicts[relPath];
    c.winner = winner;
    c.losers.insert(loser);
}

void ConflictIndex::clear() {
    m_conflicts.clear();
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

QStringList ConflictIndex::winningFilesOf(const QString& modId) const {
    QStringList result;
    for (auto it = m_conflicts.cbegin(); it != m_conflicts.cend(); ++it)
        if (it.value().winner == modId && !it.value().losers.isEmpty())
            result.append(it.key());
    return result;
}

QStringList ConflictIndex::losingFilesOf(const QString& modId) const {
    QStringList result;
    for (auto it = m_conflicts.cbegin(); it != m_conflicts.cend(); ++it)
        if (it.value().losers.contains(modId))
            result.append(it.key());
    return result;
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
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
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
