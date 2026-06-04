#pragma once
#include <QWidget>
#include <QSet>
#include <QStringList>
#include "deploy/ConflictIndex.h"
#include "core/Profile.h"

class QStackedWidget;
class QLabel;
class QLineEdit;
class QCheckBox;

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

private slots:
    void onFileActivated(const QString& fullPath);
    void onFileSaved(const QString& filePath);
    void onSplitDropped();

private:
    Profile*      m_profile = nullptr;
    ConflictIndex m_conflicts;
    QStringList   m_selection;

    QLineEdit*      m_search;
    QCheckBox*      m_showAll;
    QString         m_filter;        // current search text
    bool            m_showAllFiles = false; // mirror of m_showAll

    QStackedWidget* m_stack;
    ModFileTree*    m_singleTree;   // page 0: single mod or game dir
    QLabel*         m_placeholder;  // page 1: "Nothing to see here"
    QWidget*        m_splitPage;    // page 2: two trees
    ModFileTree*    m_splitLeft;
    ModFileTree*    m_splitRight;

    QString m_editTrackingRoot;  // staging root whose edits we last loaded
    QSet<QString> m_editedRelPaths;

    void refresh();
    void applyFilter();
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
