#pragma once
#include <QWidget>
#include <QHash>
#include <QPair>
#include <QList>
#include <QElapsedTimer>
class QTableWidget;
class QTableWidgetItem;
class QTimer;
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
    // Pause/resume an in-progress download (wired to DownloadManager in MainWindow,
    // mirroring cancelRequested).
    void pauseRequested(const QString& fileName);
    void resumeRequested(const QString& fileName);
protected:
    // A style sheet is set on the table (item padding), which sets WA_StyleSheet
    // and blocks Qt's automatic propagation of application-font (zoom) changes.
    // Re-assert the app font here so the table scales with Ctrl +/- like the
    // mod/plugin lists (which carry no direct style sheet).
    void changeEvent(QEvent* e) override;
    // Apply content-fit + fill-Name defaults on first show (viewport width is known
    // by then), unless the user has persisted column widths to restore.
    void showEvent(QShowEvent* e) override;
private:
    void showContextMenu(const QPoint& pos);
    void saveHeaderState();
    bool m_didAutoSize = false;
    QTimer* m_headerSaveTimer = nullptr;
    // Repaint the status cell for every file with a pending progress tick. Driven by
    // m_progressTimer so a burst of network-read ticks coalesces into one repaint
    // instead of a setText per chunk.
    void flushDownloadProgress();
    void applyFilters();
    // Recompute every column's width from the current font's metrics (so they scale
    // with Ctrl +/- zoom, not stay a fixed pixel constant). The last column
    // (Downloaded) then stretches to fill via stretchLastSection.
    void applyColumnWidths();
    // Bytes/sec from a short moving average of received-byte samples (0 if unknown).
    double currentRate(const QString& fileName) const;
    // Build the Status cell {sideText, tooltip} for an in-progress file, including a
    // live rate + ETA beside the icon ("3.2 MB/s <dot> 0:45") once a rate is known.
    QPair<QString,QString> formatActiveProgress(const QString& fileName,
                                                qint64 received, qint64 total) const;
    QTableWidget* m_table;
    Profile* m_profile = nullptr;
    bool m_hideInstalled = false;
    bool m_hideNotInstalled = false;
    // The default (newest-first) sort is applied once on first populate; later
    // refresh() ticks preserve whatever sort the user has chosen instead.
    bool m_defaultSortApplied = false;
    QHash<QString,int> m_activeRows; // fileName -> table row for in-progress downloads
    // Latest {received,total} per in-progress file awaiting a coalesced repaint.
    QHash<QString,QPair<qint64,qint64>> m_pendingProgress;
    QTimer* m_progressTimer = nullptr; // ~120ms flush of m_pendingProgress
    QList<QPair<QString,QString>> m_failed; // {fileName, error}
    // Rate/ETA: a monotonic clock plus a short window of (elapsedMs, receivedBytes)
    // samples per active file, and the last {received,total} seen (used to snapshot
    // a download's progress at the moment it is paused).
    QElapsedTimer m_clock;
    QHash<QString,QList<QPair<qint64,qint64>>> m_rateSamples;
    QHash<QString,QPair<qint64,qint64>> m_latestProgress;
    // Paused downloads shown as persistent "Paused" rows (fileName -> {received,total}).
    // Held here so a refresh() rebuild keeps the row (the worker gets no progress
    // ticks while paused, so it can't self-heal like a live download row does).
    QHash<QString,QPair<qint64,qint64>> m_pausedDownloads;
};
}
