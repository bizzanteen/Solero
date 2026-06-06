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
    void showGameDir(const QString& gameDir,
                    const QHash<QString, QString>& ownerByRelPath, // relPath -> mod display name
                    const QColor& accent);

    const QString& stagingRoot() const { return m_stagingRoot; }

signals:
    void fileActivated(const QString& fullPath);
    void filesDropped(); // a file was dropped in from another tree
    // Right-click "Hide/Unhide file in this mod" toggled. nowHidden = the new state.
    void hideToggled(const QString& modId, const QString& relPath, bool nowHidden);

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void buildTree(const QString& rootDir,
                   const std::function<void(QTreeWidgetItem*, const QString&)>& decorate);

    QString m_stagingRoot; // empty in game-dir mode (drops disabled)
    QString m_modId;       // owning mod id (empty in game-dir mode)
    QSet<QString> m_hiddenRelPaths; // files hidden within m_modId
};

} // namespace solero
