#include "DataTab.h"
#include "core/AppConfig.h"
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QDir>
#include <QDirIterator>
#include <QHeaderView>
#include <QColor>
#include <QMap>

namespace solero {

DataTab::DataTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({"File", "Status"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->resizeSection(1, 160);
    m_tree->setRootIsDecorated(true);
    layout->addWidget(m_tree);
}

void DataTab::setProfile(Profile* profile) { m_profile = profile; refresh(); }
void DataTab::setConflictIndex(const ConflictIndex& index) { m_conflicts = index; refresh(); }
void DataTab::showMod(const QString& modId) { m_currentModId = modId; refresh(); }

void DataTab::refresh() {
    m_tree->clear();
    if (m_currentModId.isEmpty()) {
        m_tree->addTopLevelItem(new QTreeWidgetItem({"(no mod selected)"}));
        return;
    }

    QString stagingRoot;
    if (m_currentModId == "__overwrite__")
        stagingRoot = AppConfig::instance().gameDir() + "/.solero-overwrite";
    else
        stagingRoot = AppConfig::instance().stagingDir() + "/" + m_currentModId;

    QDir root(stagingRoot);
    if (!root.exists()) {
        m_tree->addTopLevelItem(new QTreeWidgetItem({"(mod staging directory not found)"}));
        return;
    }

    QMap<QString, QTreeWidgetItem*> dirItems;
    QDirIterator it(stagingRoot, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString fullPath = it.next();
        QString relPath  = fullPath.mid(stagingRoot.length() + 1);
        QStringList parts = relPath.split('/');

        QTreeWidgetItem* parent = nullptr;
        QString accumulated;
        for (int i = 0; i < parts.size() - 1; ++i) {
            accumulated += (i > 0 ? "/" : "") + parts[i];
            if (!dirItems.contains(accumulated)) {
                auto* dirItem = parent
                    ? new QTreeWidgetItem(parent, {parts[i], ""})
                    : new QTreeWidgetItem(m_tree, {parts[i], ""});
                dirItem->setExpanded(true);
                dirItems[accumulated] = dirItem;
                parent = dirItem;
            } else {
                parent = dirItems[accumulated];
            }
        }

        QString filename = parts.last();
        QString status;
        QColor  color;
        if (m_conflicts.hasConflict(relPath)) {
            if (m_conflicts.winnerOf(relPath) == m_currentModId) {
                status = QString("beats %1 mod(s)").arg(m_conflicts.losersOf(relPath).size());
                color  = QColor("#27ae60");
            } else {
                status = "overwritten by: " + m_conflicts.winnerOf(relPath);
                color  = QColor("#c0392b");
            }
        }

        auto* item = parent
            ? new QTreeWidgetItem(parent, {filename, status})
            : new QTreeWidgetItem(m_tree, {filename, status});
        if (color.isValid()) item->setForeground(1, color);
    }

    if (m_tree->topLevelItemCount() == 0)
        m_tree->addTopLevelItem(new QTreeWidgetItem({"(staging directory is empty)"}));
}

} // namespace solero
