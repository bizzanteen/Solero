#include "ConflictsTab.h"
#include <QVBoxLayout>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QFont>
#include <QColor>
#include <QMenu>

namespace solero {

// Roles stashed on each file row so a double-click can navigate to it.
static constexpr int kRoleModId   = Qt::UserRole + 1;
static constexpr int kRoleRelPath = Qt::UserRole + 2;

ConflictsTab::ConflictsTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("Search conflicts") + QChar(0x2026));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& t){
        m_filterText = t.trimmed();
        applyFilter();
    });
    layout->addWidget(m_filter);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({"File", "Detail"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->resizeSection(1, 200);
    m_tree->setRootIsDecorated(true);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
        QString modId = item->data(0, kRoleModId).toString();
        if (modId.isEmpty()) return; // group header or placeholder
        emit fileActivated(modId, item->data(0, kRoleRelPath).toString());
    });
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QWidget::customContextMenuRequested,
            this, &ConflictsTab::showContextMenu);
    layout->addWidget(m_tree);
}

QString ConflictsTab::modDisplayName(const QString& modId) const {
    if (!m_profile) return modId;
    if (const auto* e = m_profile->modList().findById(modId)) return e->name;
    return modId;
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
            QStringList loserNames;
            for (const auto& id : losers) loserNames << modDisplayName(id);
            QString detail = QString("beats: %1").arg(loserNames.join(", "));
            const QString forced = m_profile ? m_profile->winnerOverride(path) : QString();
            if (!forced.isEmpty())
                detail += QString("  [forced winner: %1]").arg(modDisplayName(forced));
            auto* item = new QTreeWidgetItem(winRoot, {path, detail});
            item->setForeground(1, QColor("#27ae60"));
            item->setData(0, kRoleModId, m_currentModId);
            item->setData(0, kRoleRelPath, path);
        }
    }

    if (!losing.isEmpty()) {
        auto* loseRoot = new QTreeWidgetItem(m_tree, {QString("LOSING (%1 files)").arg(losing.size()), ""});
        QFont f; f.setBold(true); loseRoot->setFont(0, f);
        loseRoot->setForeground(0, QColor("#c0392b"));
        loseRoot->setExpanded(true);
        for (const auto& path : losing) {
            QString detail = QString("won by: %1").arg(modDisplayName(m_conflicts.winnerOf(path)));
            const QString forced = m_profile ? m_profile->winnerOverride(path) : QString();
            if (!forced.isEmpty())
                detail += QString("  [forced winner: %1]").arg(modDisplayName(forced));
            auto* item = new QTreeWidgetItem(loseRoot, {path, detail});
            item->setForeground(1, QColor("#c0392b"));
            item->setData(0, kRoleModId, m_currentModId);
            item->setData(0, kRoleRelPath, path);
        }
    }

    applyFilter();
}

void ConflictsTab::applyFilter() {
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* root = m_tree->topLevelItem(i);
        int visibleChildren = 0;
        for (int c = 0; c < root->childCount(); ++c) {
            QTreeWidgetItem* child = root->child(c);
            bool hide = !m_filterText.isEmpty()
                        && !child->text(0).contains(m_filterText, Qt::CaseInsensitive);
            child->setHidden(hide);
            if (!hide) ++visibleChildren;
        }
        // Hide a group header whose children are all filtered out.
        root->setHidden(root->childCount() > 0 && visibleChildren == 0);
    }
}

void ConflictsTab::showContextMenu(const QPoint& pos) {
    if (!m_profile) return;
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item) return;
    const QString modId   = item->data(0, kRoleModId).toString();   // == m_currentModId
    const QString relPath = item->data(0, kRoleRelPath).toString();
    if (modId.isEmpty() || relPath.isEmpty()) return; // group header / placeholder

    QMenu menu(this);
    const QString forced = m_profile->winnerOverride(relPath);
    // Force the currently-shown mod to win this path on the next deploy.
    if (forced != modId) {
        menu.addAction(QString("Set \"%1\" as winner for this file").arg(modDisplayName(modId)),
                       this, [this, relPath, modId]{
            m_profile->setWinnerOverride(relPath, modId);
            m_profile->save();
            emit fileRulesChanged();
            refresh();
        });
    }
    if (!forced.isEmpty()) {
        if (!menu.isEmpty()) menu.addSeparator(); // separate Set from Clear when both show
        menu.addAction(QString("Clear winner override (currently: %1)").arg(modDisplayName(forced)),
                       this, [this, relPath]{
            m_profile->clearWinnerOverride(relPath);
            m_profile->save();
            emit fileRulesChanged();
            refresh();
        });
    }
    if (!menu.isEmpty())
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

} // namespace solero
