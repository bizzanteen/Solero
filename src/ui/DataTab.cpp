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
#include <QLineEdit>
#include <QToolButton>
#include <QIcon>
#include <QSplitter>
#include <QPalette>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>

namespace solero {

DataTab::DataTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Top bar: search + Show-mod/all toggle + Collapse/Expand toggle
    auto* topBar = new QHBoxLayout;
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Search files") + QChar(0x2026));

    // Two checkable icon toggles (were label-flipping text buttons). The checked
    // state is the visual cue; the tooltip states the ACTION. Fall back to a short
    // glyph label only when the icon theme lacks the icon (never a byte escape).
    auto makeToggle = [this](const QString& themeIcon, const QString& altTheme,
                             QChar fallback, bool checked) {
        auto* b = new QToolButton(this);
        b->setCheckable(true);
        b->setChecked(checked);
        b->setAutoRaise(true);
        const QIcon ic = QIcon::fromTheme(themeIcon, QIcon::fromTheme(altTheme));
        if (ic.isNull()) b->setText(QString(fallback));
        else             b->setIcon(ic);
        return b;
    };

    m_showAllFiles = AppConfig::instance().dataShowAllFiles();
    m_showAllBtn = makeToggle(QStringLiteral("view-visible"),
                              QStringLiteral("view-list-details"),
                              QChar(0x2261), m_showAllFiles); // triple-bar = list
    updateShowAllText();

    m_collapseBtn = makeToggle(QStringLiteral("view-list-tree"),
                               QStringLiteral("format-indent-less"),
                               QChar(0x25B8), m_collapsed); // right-pointing triangle
    updateCollapseText();

    topBar->addWidget(m_search, 1);
    topBar->addWidget(m_showAllBtn, 0);
    topBar->addWidget(m_collapseBtn, 0);
    layout->addLayout(topBar);

    // Debounce search input: re-filtering walks the whole (possibly huge) tree,
    // so coalesce keystrokes and only filter once the user pauses typing.
    m_filterDebounce = new QTimer(this);
    m_filterDebounce->setSingleShot(true);
    m_filterDebounce->setInterval(200);
    connect(m_filterDebounce, &QTimer::timeout, this, [this]{ applyFilter(); });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_filter = text;
        m_filterDebounce->start();
    });
    connect(m_showAllBtn, &QToolButton::toggled, this, [this](bool on) {
        m_showAllFiles = on;
        updateShowAllText();
        scheduleRefresh();
    });
    connect(m_collapseBtn, &QToolButton::toggled, this, [this](bool on) {
        m_collapsed = on;
        updateCollapseText();
        applyFolderState();
    });

    m_stack = new QStackedWidget(this);

    // Page 0: single tree (mod files or game dir)
    m_singleTree = new ModFileTree(this);
    connect(m_singleTree, &ModFileTree::fileActivated, this, &DataTab::onFileActivated);
    connect(m_singleTree, &ModFileTree::hideToggled, this, &DataTab::onHideToggled);
    connect(m_singleTree, &ModFileTree::renameRequested, this, &DataTab::onRenameRequested);
    connect(m_singleTree, &ModFileTree::deleteRequested, this, &DataTab::onDeleteRequested);
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
    connect(m_splitLeft,  &ModFileTree::hideToggled,   this, &DataTab::onHideToggled);
    connect(m_splitRight, &ModFileTree::hideToggled,   this, &DataTab::onHideToggled);
    connect(m_splitLeft,  &ModFileTree::renameRequested, this, &DataTab::onRenameRequested);
    connect(m_splitRight, &ModFileTree::renameRequested, this, &DataTab::onRenameRequested);
    connect(m_splitLeft,  &ModFileTree::deleteRequested, this, &DataTab::onDeleteRequested);
    connect(m_splitRight, &ModFileTree::deleteRequested, this, &DataTab::onDeleteRequested);
    m_stack->addWidget(m_splitPage);

    layout->addWidget(m_stack);

    m_stack->setCurrentWidget(m_singleTree);
    showGameDirectory(); // nothing selected initially -> game dir view
}

void DataTab::setProfile(Profile* profile) { m_profile = profile; scheduleRefresh(); }
void DataTab::setConflictIndex(const ConflictIndex& index) { m_conflicts = index; scheduleRefresh(); }

void DataTab::setSelection(const QStringList& ids) {
    m_selection = ids;
    scheduleRefresh();
}

void DataTab::scheduleRefresh() {
    // Many call sites set profile + conflicts + selection back-to-back; without
    // coalescing each would trigger a full directory walk and tree rebuild. Defer
    // to the next event-loop turn so a burst collapses into one refresh().
    if (m_refreshPending) return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshPending = false;
        refresh();
    });
}

QColor DataTab::accentColor() const {
    return palette().color(QPalette::Highlight);
}

QString DataTab::stagingRootFor(const QString& modId) const {
    if (modId == "__overwrite__")
        return AppConfig::overwriteDir(m_profile ? m_profile->name() : QString()); // per-profile Overwrite
    // Resolve to the mod's ACTUAL staging folder, exactly as the deploy engine and
    // every other caller do (stagingFolderFor). Migrated profiles use named folders
    // (e.g. "SSE Display Tweaks"), not the raw id - using the id pointed the Data
    // pane at a nonexistent/wrong directory, so edits never reached the deployed
    // copy and appeared to revert to default.
    const QString folder = m_profile ? m_profile->stagingFolderFor(modId) : modId;
    return AppConfig::instance().stagingDir() + "/" + folder;
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

void DataTab::applyFilter() {
    m_singleTree->setFilter(m_filter);
    m_splitLeft->setFilter(m_filter);
    m_splitRight->setFilter(m_filter);
}

void DataTab::applyFolderState() {
    for (ModFileTree* t : { m_singleTree, m_splitLeft, m_splitRight }) {
        if (m_collapsed) t->collapseTree();
        else             t->expandTree();
    }
}

void DataTab::updateShowAllText() {
    // Tooltip states the action; the checked state shows what's currently active.
    m_showAllBtn->setToolTip(m_showAllFiles
        ? QStringLiteral("Showing all files - click to show only this mod's files")
        : QStringLiteral("Show all files (including base game files)"));
}

void DataTab::updateCollapseText() {
    m_collapseBtn->setToolTip(m_collapsed
        ? QStringLiteral("Folders collapsed - click to expand all folders")
        : QStringLiteral("Collapse all folders"));
}

void DataTab::refresh() {
    // Partition the selection: separators are ignored for counting; mods + overwrite count.
    QStringList modIds;
    bool onlySeparators = !m_selection.isEmpty();
    for (const auto& id : m_selection) {
        if (id == "__separator__") continue;
        onlySeparators = false;
        modIds << id;
    }
    const bool hasMods = !modIds.isEmpty();

    // A separator has no files of its own. Selecting one used to fall through to
    // "no mods -> show the whole game dir", which eagerly rebuilt the entire merged
    // Data tree (thousands of files) on every separator click - the source of the
    // lag. Show a cheap placeholder instead. (A truly empty selection - m_selection
    // empty - still falls through to the all-files view below.)
    if (onlySeparators) {
        m_showAllBtn->setEnabled(false);
        updateShowAllText();
        m_placeholder->setText("Separators have no files to show.");
        m_stack->setCurrentWidget(m_placeholder);
        return;
    }

    // With no mod selected there's nothing to show but the live game dir, so lock
    // the toggle to "Showing all files" (disabled) until a mod is selected.
    m_showAllBtn->setEnabled(hasMods);
    updateShowAllText();

    if (!hasMods) {
        showGameDirectory(); // all files (game dir), regardless of the toggle
        return;
    }
    if (m_showAllFiles) {
        showGameDirectory();
        applyFilter();
        return;
    }
    if (modIds.size() == 1) {
        showSingleMod(modIds.first());
    } else if (modIds.size() == 2) {
        showSplit(modIds.at(0), modIds.at(1));
    } else {
        // This tab compares at most two mods; guide the user back into range.
        m_placeholder->setText(
            "Select one or two mods to view or compare their files."); // 3+
        m_stack->setCurrentWidget(m_placeholder);
    }
}

void DataTab::showSingleMod(const QString& modId) {
    m_gameDirView = false;
    QString root = stagingRootFor(modId);

    QDirIterator probe(root, QDir::Files,
                       QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    bool empty = true;
    while (probe.hasNext()) {
        if (!QFileInfo(probe.next()).fileName().startsWith(".solero")) { empty = false; break; }
    }
    if (empty) {
        m_placeholder->setText("Mod folder is empty");
        m_stack->setCurrentWidget(m_placeholder);
        return;
    }

    m_editTrackingRoot = root;
    loadEditedFor(root, m_editedRelPaths);
    QSet<QString> hidden = m_profile ? m_profile->hiddenFiles().value(modId) : QSet<QString>();
    m_singleTree->showModFiles(root, modId, m_conflicts, m_editedRelPaths, accentColor(),
                               [this](const QString& id){ return modDisplayName(id); }, hidden);
    m_stack->setCurrentWidget(m_singleTree);
    applyFolderState();
    applyFilter();
}

void DataTab::showGameDirectory() {
    m_gameDirView = true;
    QString gameDir = AppConfig::instance().gameDir();
    // Build relPath -> mod display name from the on-disk DeployRecord
    QHash<QString, QString> ownerByRel;
    // Parallel maps keyed by relPath: owner modId (for the merged-view Hide
    // action) and which of those are currently hidden for their owner.
    QHash<QString, QString> ownerModIdByRel;
    QSet<QString> hiddenByRel;
    QString recPath = DeployEngine::recordPath(gameDir);
    DeployRecord rec = DeployRecord::loadFromFile(recPath);
    // Build modId -> display name once; rec.allPaths() can be tens of thousands
    // of entries and modDisplayName()'s findById() is O(mods) per call.
    QHash<QString, QString> nameById;
    if (m_profile)
        for (const ModEntry& e : m_profile->modList().entries())
            nameById.insert(e.id, e.name);
    for (const auto& relPath : rec.allPaths()) {
        const QString ownerModId = rec.ownerOf(relPath);
        ownerByRel.insert(relPath, nameById.value(ownerModId, ownerModId));
        ownerModIdByRel.insert(relPath, ownerModId);
        if (m_profile && !ownerModId.isEmpty() &&
            m_profile->isFileHidden(ownerModId, relPath))
            hiddenByRel.insert(relPath);
    }

    m_editTrackingRoot.clear();
    m_singleTree->showGameDir(gameDir, ownerByRel, ownerModIdByRel, hiddenByRel,
                              accentColor());
    m_stack->setCurrentWidget(m_singleTree);
    applyFolderState();
    applyFilter();
}

void DataTab::showSplit(const QString& modIdA, const QString& modIdB) {
    m_gameDirView = false;
    QString rootA = stagingRootFor(modIdA);
    QString rootB = stagingRootFor(modIdB);
    QSet<QString> editedA, editedB;
    loadEditedFor(rootA, editedA);
    loadEditedFor(rootB, editedB);
    m_splitLeft->setAcceptDrops(true);
    m_splitRight->setAcceptDrops(true);
    QSet<QString> hiddenA = m_profile ? m_profile->hiddenFiles().value(modIdA) : QSet<QString>();
    QSet<QString> hiddenB = m_profile ? m_profile->hiddenFiles().value(modIdB) : QSet<QString>();
    m_splitLeft->showModFiles(rootA, modIdA, m_conflicts, editedA, accentColor(),
                              [this](const QString& id){ return modDisplayName(id); }, hiddenA);
    m_splitRight->showModFiles(rootB, modIdB, m_conflicts, editedB, accentColor(),
                               [this](const QString& id){ return modDisplayName(id); }, hiddenB);
    m_stack->setCurrentWidget(m_splitPage);
    applyFolderState();
    applyFilter();
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
    // Under hardlink/symlink deploy the editor wrote in place, so the live game
    // file (same inode) already reflects the edit - no redeploy needed. Only COPY
    // mode keeps live and staging as independent files, so there mark the
    // deployment dirty to prompt a redeploy that pushes the edit to the game.
    if (AppConfig::instance().deployMode() == DeployMode::Copy)
        emit fileRulesChanged();
    scheduleRefresh();
}

void DataTab::onSplitDropped() {
    scheduleRefresh(); // rebuild both trees to reflect the copied file
}

void DataTab::onHideToggled(const QString& modId, const QString& relPath, bool hide) {
    if (!m_profile) return;
    m_profile->setFileHidden(modId, relPath, hide);
    m_profile->save();          // persist filerules.json immediately
    emit fileRulesChanged();    // -> MainWindow marks the deployment dirty
    // In the merged game-dir view the ModFileTree already restyled the clicked
    // row in place (see contextMenuEvent), so skip the expensive full-tree
    // rebuild - re-reading the DeployRecord and re-walking the whole game Data
    // dir on every click is what made hiding slow. The single-mod/split views
    // are tiny, so their cheap rebuild (restoring conflict/edited decoration) is
    // fine.
    if (!m_gameDirView)
        scheduleRefresh();      // repaint the tree with the new hidden state
}

void DataTab::onRenameRequested(const QString& modId, const QString& relPath,
                                const QString& newName, bool isFolder) {
    // MainWindow performs the rename on the mod's staging dir (and invalidates
    // its caches / marks the deployment dirty); then rebuild the tree from disk.
    emit renameRequested(modId, relPath, newName, isFolder);
    scheduleRefresh();
}

void DataTab::onDeleteRequested(const QString& modId, const QString& relPath,
                                bool isFolder) {
    emit deleteRequested(modId, relPath, isFolder);
    scheduleRefresh();
}

} // namespace solero
