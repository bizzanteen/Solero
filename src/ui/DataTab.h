#pragma once
#include <QWidget>
#include <QSet>
#include <QStringList>
#include "deploy/ConflictIndex.h"
#include "core/Profile.h"

class QStackedWidget;
class QLabel;
class QLineEdit;
class QPushButton;

namespace solero {

class ModFileTree;

class DataTab : public QWidget {
    Q_OBJECT
public:
    explicit DataTab(QWidget* parent = nullptr);
    void setProfile(Profile* profile);
    void setConflictIndex(const ConflictIndex& index);

    // Drives the view based on the current mod-list selection.
    // ids contain mod ids; "__overwrite__" for Overwrite; "__separator__" for separators.
    void setSelection(const QStringList& ids);

signals:
    // A per-file rule changed (hide/unhide) - the deployment is now dirty.
    void fileRulesChanged();
    // Rename/delete of a staged file or folder bubbles up so MainWindow performs
    // the filesystem op on the mod's staging dir + invalidates its caches.
    void renameRequested(const QString& modId, const QString& relPath,
                         const QString& newName, bool isFolder);
    void deleteRequested(const QString& modId, const QString& relPath, bool isFolder);

private slots:
    void onFileActivated(const QString& fullPath);
    void onFileSaved(const QString& filePath);
    void onSplitDropped();
    void onHideToggled(const QString& modId, const QString& relPath, bool hide);
    void onRenameRequested(const QString& modId, const QString& relPath,
                           const QString& newName, bool isFolder);
    void onDeleteRequested(const QString& modId, const QString& relPath, bool isFolder);

private:
    Profile*      m_profile = nullptr;
    ConflictIndex m_conflicts;
    QStringList   m_selection;

    QLineEdit*      m_search;
    QPushButton*    m_showAllBtn;
    QPushButton*    m_collapseBtn;
    QString         m_filter;        // current search text
    QTimer*         m_filterDebounce = nullptr; // coalesces search keystrokes
    bool            m_showAllFiles = false; // mirror of m_showAllBtn
    bool            m_collapsed = false;     // mirror of m_collapseBtn
    bool            m_gameDirView = false;   // true while the merged game-dir view is shown

    QStackedWidget* m_stack;
    ModFileTree*    m_singleTree;   // page 0: single mod or game dir
    QLabel*         m_placeholder;  // page 1: "Nothing to see here"
    QWidget*        m_splitPage;    // page 2: two trees
    ModFileTree*    m_splitLeft;
    ModFileTree*    m_splitRight;

    QString m_editTrackingRoot;  // staging root whose edits we last loaded
    QSet<QString> m_editedRelPaths;

    void refresh();
    // Coalesces bursts of setProfile/setConflictIndex/setSelection/toggle into a
    // single rebuild on the next event-loop turn (each setter used to trigger its
    // own full tree walk).
    void scheduleRefresh();
    bool m_refreshPending = false;
    void applyFilter();
    void applyFolderState();
    void updateShowAllText();
    void updateCollapseText();
    QString stagingRootFor(const QString& modId) const;
    QColor  accentColor() const;
    void showSingleMod(const QString& modId);
    void showGameDirectory();
    void showSplit(const QString& modIdA, const QString& modIdB);
    void loadEditedFor(const QString& stagingRoot, QSet<QString>& out) const;
    static QString editedMarkerPath(const QString& stagingRoot);
    QString modDisplayName(const QString& modId) const;
};

} // namespace solero
