#pragma once
#include <QTreeWidget>
#include <QSet>
#include <QHash>
#include <QColor>
#include <functional>
#include "deploy/ConflictIndex.h"

namespace solero {

// A file tree for one mod's staging directory (or the game directory).
// Supports drag-and-drop of files between two trees, decorates rows with
// conflict / edited / owner status, and emits fileActivated on double-click.
class ModFileTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit ModFileTree(QWidget* parent = nullptr);

    // Populate from a mod's staging directory. hiddenRelPaths are files hidden
    // within this mod (MO2 ".mohidden"): shown struck-through and skipped on deploy.
    void showModFiles(const QString& stagingRoot,
                      const QString& modId,
                      const ConflictIndex& conflicts,
                      const QSet<QString>& editedRelPaths,
                      const QColor& accent,
                      const std::function<QString(const QString&)>& nameOf = {},
                      const QSet<QString>& hiddenRelPaths = {});

    // Filter visible rows by a search string (matches filenames/paths).
    void setFilter(const QString& text);

    // Populate from the live game directory, colouring mod-owned files.
    // ownerModIdByRel (relPath -> owner modId) and hiddenByRel (relPaths currently
    // hidden for their owner) drive the merged-view Hide/Unhide context menu.
    void showGameDir(const QString& gameDir,
                    const QHash<QString, QString>& ownerByRelPath, // relPath -> mod display name
                    const QHash<QString, QString>& ownerModIdByRel, // relPath -> owner modId
                    const QSet<QString>& hiddenByRel,               // relPaths currently hidden
                    const QColor& accent);

    const QString& stagingRoot() const { return m_stagingRoot; }

    // Lazy-aware folder state. expandTree() on a lazy game-dir tree only opens
    // the already-materialized levels (no full walk); collapseTree() collapses
    // without re-walking. Prefer these over QTreeWidget::expandAll/collapseAll.
    void expandTree();
    void collapseTree();

signals:
    void fileActivated(const QString& fullPath);
    void filesDropped(); // a file was dropped in from another tree
    // Right-click "Hide/Unhide file in this mod" toggled. nowHidden = the new state.
    void hideToggled(const QString& modId, const QString& relPath, bool nowHidden);
    // Right-click "Rename…" on a file or folder. newName is a validated basename
    // (no separators, no collision) within the same parent directory.
    void renameRequested(const QString& modId, const QString& relPath,
                         const QString& newName, bool isFolder);
    // Right-click "Delete" on a file or folder (already confirmed by the user).
    void deleteRequested(const QString& modId, const QString& relPath, bool isFolder);

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    using Decorator = std::function<void(QTreeWidgetItem*, const QString&)>;

    // Eager full-tree build (used for mod-staging trees, which are small).
    void buildTree(const QString& rootDir, const Decorator& decorate);

    // Lazy build (used for the live game directory, which can hold thousands of
    // files): only the top level is materialized; each folder gets a placeholder
    // child and is filled on demand when the user expands it (onItemExpanded).
    void buildTreeLazy(const QString& rootDir, const Decorator& decorate);
    void onItemExpanded(QTreeWidgetItem* item);
    void populateChildren(QTreeWidgetItem* dir); // fill one collapsed dir level
    void materializeAll();                        // force the whole lazy tree open
    QTreeWidgetItem* makeFileItem(QTreeWidgetItem* parent, const QString& fullPath,
                                  const QString& relPath, const QString& filename);
    QTreeWidgetItem* makeDirItem(QTreeWidgetItem* parent, const QString& fullPath,
                                 const QString& relPath, const QString& name);

    QString m_stagingRoot; // empty in game-dir mode (drops disabled)
    QString m_modId;       // owning mod id (empty in game-dir mode)
    QSet<QString> m_hiddenRelPaths; // files hidden within m_modId

    // Game-dir (merged) mode: per-file owner modId and current hidden state, used
    // to surface the same Hide/Unhide action there. Empty in single-mod mode.
    QHash<QString, QString> m_gameOwnerModId; // relPath -> owner modId
    QSet<QString>           m_gameHidden;     // relPaths currently hidden

    // Lazy game-dir state. m_lazyRoot is non-empty only while a lazy tree is live.
    bool      m_lazy = false;
    QString   m_lazyRoot;
    Decorator m_decorate;
};

} // namespace solero
