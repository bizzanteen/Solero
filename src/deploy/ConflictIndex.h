#pragma once
#include <QString>
#include <QHash>
#include <QSet>
#include <QStringList>

namespace solero {

struct FileConflict {
    QString winner;
    QSet<QString> losers;
};

class ConflictIndex {
public:
    void setWinner(const QString& relPath, const QString& modId);
    void recordConflict(const QString& relPath, const QString& winner, const QString& loser);
    void clear();

    QString winnerOf(const QString& relPath) const;
    QSet<QString> losersOf(const QString& relPath) const;
    bool hasConflict(const QString& relPath) const;

    QStringList winningFilesOf(const QString& modId) const;
    QStringList losingFilesOf(const QString& modId) const;
    QStringList conflictedPaths() const;

    bool saveToFile(const QString& path) const;
    static ConflictIndex loadFromFile(const QString& path);

private:
    QHash<QString, FileConflict> m_conflicts;

    // modId -> its winning/losing paths, so the lookups don't rescan every
    // conflicted path. Built lazily and dropped on any mutation.
    void buildReverse() const;
    mutable bool m_reverseBuilt = false;
    mutable QHash<QString, QStringList> m_winningByMod; // modId -> paths it wins (with losers)
    mutable QHash<QString, QStringList> m_losingByMod;  // modId -> paths where it's a loser
};

} // namespace solero
