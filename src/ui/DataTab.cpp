#include "DataTab.h"
#include "FileEditorDialog.h"
#include "core/AppConfig.h"
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QDir>
#include <QDirIterator>
#include <QColor>
#include <QFont>
#include <QMap>
#include <QHeaderView>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>

namespace solero {

// Roles for stashing per-item data on file rows
static constexpr int RoleFullPath = Qt::UserRole;
static constexpr int RoleRelPath  = Qt::UserRole + 1;

DataTab::DataTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({"File", "Status"});
    // Resizable columns; last section stretches to fill remaining width.
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setColumnWidth(0, 380);
    m_tree->setColumnWidth(1, 180);
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &DataTab::onItemDoubleClicked);
}

void DataTab::setProfile(Profile* profile) { m_profile = profile; refresh(); }
void DataTab::setConflictIndex(const ConflictIndex& index) { m_conflicts = index; refresh(); }
void DataTab::showMod(const QString& modId) { m_currentModId = modId; refresh(); }

QString DataTab::stagingRootFor(const QString& modId) const {
    if (modId == "__overwrite__")
        return AppConfig::instance().gameDir() + "/.solero-overwrite";
    return AppConfig::instance().stagingDir() + "/" + modId;
}

QString DataTab::editedMarkerPath(const QString& stagingRoot) {
    return stagingRoot + "/.solero-edited.json";
}

void DataTab::loadEdited(const QString& stagingRoot) {
    m_editedRelPaths.clear();
    QFile f(editedMarkerPath(stagingRoot));
    if (!f.open(QIODevice::ReadOnly)) return;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array())
        m_editedRelPaths.insert(v.toString());
}

void DataTab::saveEdited(const QString& stagingRoot) const {
    QJsonArray arr;
    for (const auto& p : m_editedRelPaths) arr.append(p);
    QFile f(editedMarkerPath(stagingRoot));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void DataTab::refresh() {
    m_tree->clear();
    if (m_currentModId.isEmpty()) {
        m_tree->addTopLevelItem(new QTreeWidgetItem({"Select a mod to view its files"}));
        m_currentStagingRoot.clear();
        return;
    }

    m_currentStagingRoot = stagingRootFor(m_currentModId);
    loadEdited(m_currentStagingRoot);

    QDir root(m_currentStagingRoot);
    if (!root.exists()) {
        m_tree->addTopLevelItem(new QTreeWidgetItem(
            {m_currentModId == "__overwrite__"
                 ? "Overwrite folder is empty"
                 : "(mod staging directory not found)"}));
        return;
    }

    QMap<QString, QTreeWidgetItem*> dirItems;
    QDirIterator it(m_currentStagingRoot, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString fullPath = it.next();
        QString relPath  = fullPath.mid(m_currentStagingRoot.length() + 1);

        // Skip Solero's own marker files
        if (relPath.startsWith(".solero")) continue;

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

        bool edited = m_editedRelPaths.contains(relPath);
        if (edited)
            status = status.isEmpty() ? QStringLiteral("✎ edited")
                                      : QStringLiteral("✎ edited • ") + status;

        auto* item = parent
            ? new QTreeWidgetItem(parent, {filename, status})
            : new QTreeWidgetItem(m_tree, {filename, status});
        if (color.isValid()) item->setForeground(1, color);
        if (edited) {
            QFont f = item->font(0); f.setItalic(true); item->setFont(0, f);
            item->setForeground(1, QColor("#e67e22")); // orange for edited
        }
        item->setData(0, RoleFullPath, fullPath);
        item->setData(0, RoleRelPath,  relPath);
    }

    if (m_tree->topLevelItemCount() == 0)
        m_tree->addTopLevelItem(new QTreeWidgetItem({"(staging directory is empty)"}));
}

void DataTab::onItemDoubleClicked(QTreeWidgetItem* item, int /*column*/) {
    if (!item) return;
    QString fullPath = item->data(0, RoleFullPath).toString();
    if (fullPath.isEmpty()) return; // a directory row, not a file

    auto* editor = new FileEditorDialog(fullPath, this);
    connect(editor, &FileEditorDialog::fileSaved, this, &DataTab::onFileSaved);
    editor->show();
}

void DataTab::onFileSaved(const QString& filePath) {
    if (m_currentStagingRoot.isEmpty()) return;
    if (!filePath.startsWith(m_currentStagingRoot)) return;
    QString relPath = filePath.mid(m_currentStagingRoot.length() + 1);
    m_editedRelPaths.insert(relPath);
    saveEdited(m_currentStagingRoot);
    refresh();
}

} // namespace solero
