#include "ConflictsTab.h"
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QFont>
#include <QColor>

namespace solero {

ConflictsTab::ConflictsTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({"File", "Detail"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->resizeSection(1, 200);
    m_tree->setRootIsDecorated(true);
    layout->addWidget(m_tree);
}

void ConflictsTab::setConflictIndex(const ConflictIndex& index) { m_conflicts = index; refresh(); }
void ConflictsTab::showMod(const QString& modId) { m_currentModId = modId; refresh(); }

void ConflictsTab::refresh() {
    m_tree->clear();
    if (m_currentModId.isEmpty()) {
        m_tree->addTopLevelItem(new QTreeWidgetItem({"(no mod selected)"}));
        return;
    }

    auto winning = m_conflicts.winningFilesOf(m_currentModId);
    auto losing  = m_conflicts.losingFilesOf(m_currentModId);

    if (winning.isEmpty() && losing.isEmpty()) {
        m_tree->addTopLevelItem(new QTreeWidgetItem({"No conflicts for this mod."}));
        return;
    }

    if (!winning.isEmpty()) {
        auto* winRoot = new QTreeWidgetItem(m_tree, {QString("WINNING (%1 files)").arg(winning.size()), ""});
        QFont f; f.setBold(true); winRoot->setFont(0, f);
        winRoot->setForeground(0, QColor("#27ae60"));
        winRoot->setExpanded(true);
        for (const auto& path : winning) {
            auto losers = m_conflicts.losersOf(path);
            QStringList loserNames(losers.begin(), losers.end());
            auto* item = new QTreeWidgetItem(winRoot, {path, QString("beats: %1").arg(loserNames.join(", "))});
            item->setForeground(1, QColor("#27ae60"));
        }
    }

    if (!losing.isEmpty()) {
        auto* loseRoot = new QTreeWidgetItem(m_tree, {QString("LOSING (%1 files)").arg(losing.size()), ""});
        QFont f; f.setBold(true); loseRoot->setFont(0, f);
        loseRoot->setForeground(0, QColor("#c0392b"));
        loseRoot->setExpanded(true);
        for (const auto& path : losing) {
            auto* item = new QTreeWidgetItem(loseRoot, {path, QString("won by: %1").arg(m_conflicts.winnerOf(path))});
            item->setForeground(1, QColor("#c0392b"));
        }
    }
}

} // namespace solero
