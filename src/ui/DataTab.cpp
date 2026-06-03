#include "DataTab.h"
#include "ModFileTree.h"
#include "FileEditorDialog.h"
#include "core/AppConfig.h"
#include "deploy/DeployEngine.h"
#include "deploy/DeployRecord.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QLabel>
#include <QSplitter>
#include <QPalette>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>

namespace solero {

DataTab::DataTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(this);

    // Page 0: single tree (mod files or game dir)
    m_singleTree = new ModFileTree(this);
    connect(m_singleTree, &ModFileTree::fileActivated, this, &DataTab::onFileActivated);
    m_stack->addWidget(m_singleTree);

    // Page 1: placeholder
    m_placeholder = new QLabel("Nothing to see here", this);
    m_placeholder->setAlignment(Qt::AlignCenter);
    QFont pf = m_placeholder->font(); pf.setItalic(true); m_placeholder->setFont(pf);
    m_placeholder->setStyleSheet("color: gray;");
    m_stack->addWidget(m_placeholder);

    // Page 2: split view (two trees)
    m_splitPage = new QWidget(this);
    auto* splitLayout = new QHBoxLayout(m_splitPage);
    splitLayout->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter(Qt::Horizontal, m_splitPage);
    m_splitLeft  = new ModFileTree(m_splitPage);
    m_splitRight = new ModFileTree(m_splitPage);
    splitter->addWidget(m_splitLeft);
    splitter->addWidget(m_splitRight);
    splitter->setSizes({400, 400});
    splitLayout->addWidget(splitter);
    connect(m_splitLeft,  &ModFileTree::fileActivated, this, &DataTab::onFileActivated);
    connect(m_splitRight, &ModFileTree::fileActivated, this, &DataTab::onFileActivated);
    connect(m_splitLeft,  &ModFileTree::filesDropped,  this, &DataTab::onSplitDropped);
    connect(m_splitRight, &ModFileTree::filesDropped,  this, &DataTab::onSplitDropped);
    m_stack->addWidget(m_splitPage);

    layout->addWidget(m_stack);

    m_stack->setCurrentWidget(m_singleTree);
    showGameDirectory(); // nothing selected initially -> game dir view
}

void DataTab::setProfile(Profile* profile) { m_profile = profile; refresh(); }
void DataTab::setConflictIndex(const ConflictIndex& index) { m_conflicts = index; refresh(); }

void DataTab::setSelection(const QStringList& ids) {
    m_selection = ids;
    refresh();
}

QColor DataTab::accentColor() const {
    return palette().color(QPalette::Highlight);
}

QString DataTab::stagingRootFor(const QString& modId) const {
    if (modId == "__overwrite__")
        return AppConfig::instance().gameDir() + "/.solero-overwrite";
    return AppConfig::instance().stagingDir() + "/" + modId;
}

QString DataTab::editedMarkerPath(const QString& stagingRoot) {
    return stagingRoot + "/.solero-edited.json";
}

void DataTab::loadEditedFor(const QString& stagingRoot, QSet<QString>& out) const {
    out.clear();
    QFile f(editedMarkerPath(stagingRoot));
    if (!f.open(QIODevice::ReadOnly)) return;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array())
        out.insert(v.toString());
}

QString DataTab::modDisplayName(const QString& modId) const {
    if (!m_profile) return modId;
    if (const auto* e = m_profile->modList().findById(modId)) return e->name;
    return modId;
}

void DataTab::refresh() {
    // Partition the selection: separators are ignored for counting; mods + overwrite count.
    QStringList modIds;
    int separatorCount = 0;
    for (const auto& id : m_selection) {
        if (id == "__separator__") { ++separatorCount; continue; }
        modIds << id;
    }

    if (modIds.isEmpty()) {
        if (separatorCount > 0) {
            // Only separator(s) selected
            m_stack->setCurrentWidget(m_placeholder);
        } else {
            // Nothing selected -> show live game directory
            showGameDirectory();
        }
        return;
    }
    if (modIds.size() == 1) {
        showSingleMod(modIds.first());
    } else if (modIds.size() == 2) {
        showSplit(modIds.at(0), modIds.at(1));
    } else {
        m_stack->setCurrentWidget(m_placeholder); // 3+
    }
}

void DataTab::showSingleMod(const QString& modId) {
    QString root = stagingRootFor(modId);
    m_editTrackingRoot = root;
    loadEditedFor(root, m_editedRelPaths);
    m_singleTree->showModFiles(root, modId, m_conflicts, m_editedRelPaths, accentColor());
    m_stack->setCurrentWidget(m_singleTree);
}

void DataTab::showGameDirectory() {
    QString gameDir = AppConfig::instance().gameDir();
    // Build relPath -> mod display name from the on-disk DeployRecord
    QHash<QString, QString> ownerByRel;
    QString recPath = DeployEngine::recordPath(gameDir);
    DeployRecord rec = DeployRecord::loadFromFile(recPath);
    for (const auto& relPath : rec.allPaths())
        ownerByRel.insert(relPath, modDisplayName(rec.ownerOf(relPath)));

    m_editTrackingRoot.clear();
    m_singleTree->showGameDir(gameDir, ownerByRel, accentColor());
    m_stack->setCurrentWidget(m_singleTree);
}

void DataTab::showSplit(const QString& modIdA, const QString& modIdB) {
    QString rootA = stagingRootFor(modIdA);
    QString rootB = stagingRootFor(modIdB);
    QSet<QString> editedA, editedB;
    loadEditedFor(rootA, editedA);
    loadEditedFor(rootB, editedB);
    m_splitLeft->setAcceptDrops(true);
    m_splitRight->setAcceptDrops(true);
    m_splitLeft->showModFiles(rootA, modIdA, m_conflicts, editedA, accentColor());
    m_splitRight->showModFiles(rootB, modIdB, m_conflicts, editedB, accentColor());
    m_stack->setCurrentWidget(m_splitPage);
}

void DataTab::onFileActivated(const QString& fullPath) {
    if (fullPath.isEmpty()) return;
    auto* editor = new FileEditorDialog(fullPath, this);
    connect(editor, &FileEditorDialog::fileSaved, this, &DataTab::onFileSaved);
    editor->show();
}

void DataTab::onFileSaved(const QString& filePath) {
    if (m_editTrackingRoot.isEmpty()) return;
    if (!filePath.startsWith(m_editTrackingRoot)) return;
    QString relPath = filePath.mid(m_editTrackingRoot.length() + 1);
    m_editedRelPaths.insert(relPath);
    // Persist
    QJsonArray arr;
    for (const auto& p : m_editedRelPaths) arr.append(p);
    QFile f(editedMarkerPath(m_editTrackingRoot));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    refresh();
}

void DataTab::onSplitDropped() {
    refresh(); // rebuild both trees to reflect the copied file
}

} // namespace solero
