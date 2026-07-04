#pragma once
#include <QWidget>
#include <QHash>
#include <QPair>
#include <QList>
class QTableWidget;
class QTableWidgetItem;
namespace solero { class Profile; }
namespace solero {
class DownloadsTab : public QWidget {
    Q_OBJECT
public:
    explicit DownloadsTab(QWidget* parent = nullptr);
    void refresh();
    void setProfile(Profile* profile); // for "installed" status
    // Show/update a transient in-progress download row at the top of the table.
    void setDownloadProgress(const QString& fileName, qint64 received, qint64 total);
    // Failed downloads to show as persistent "Failed" rows ({fileName, error}).
    void setFailedDownloads(const QList<QPair<QString,QString>>& failures);
signals:
    void installRequested(const QString& archivePath);
    void cancelRequested(const QString& fileName);
    void retryRequested(const QString& fileName);
protected:
    // A style sheet is set on the table (item padding), which sets WA_StyleSheet
    // and blocks Qt's automatic propagation of application-font (zoom) changes.
    // Re-assert the app font here so the table scales with Ctrl +/- like the
    // mod/plugin lists (which carry no direct style sheet).
    void changeEvent(QEvent* e) override;
    // Re-flex the Name column so the row always spans the viewport when the pane
    // is resized.
    void resizeEvent(QResizeEvent* e) override;
private:
    void showContextMenu(const QPoint& pos);
    void applyFilters();
    // Size the Name column to soak up whatever width the other (Interactive)
    // columns leave, so the row spans the full viewport. Name is Interactive too,
    // so this is a deliberate one-shot resize (startup / pane / other-column
    // resize), not a Stretch section - which keeps divider drags feeling natural
    // (no inverted-drag, same fix as the mod/plugin lists).
    void fillNameColumn();
    // Recompute the data columns' widths from the current font's metrics (so they
    // scale with Ctrl +/- zoom, not stay a fixed pixel constant), then re-fill Name.
    void applyColumnWidths();
    QTableWidget* m_table;
    bool m_fillingName = false; // guards fillNameColumn's own sectionResized re-entry
    Profile* m_profile = nullptr;
    bool m_hideInstalled = false;
    bool m_hideNotInstalled = false;
    // The default (newest-first) sort is applied once on first populate; later
    // refresh() ticks preserve whatever sort the user has chosen instead.
    bool m_defaultSortApplied = false;
    QHash<QString,int> m_activeRows; // fileName -> table row for in-progress downloads
    QList<QPair<QString,QString>> m_failed; // {fileName, error}
};
}
