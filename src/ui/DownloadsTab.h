#pragma once
#include <QWidget>
#include <QHash>
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
signals:
    void installRequested(const QString& archivePath);
private:
    void showContextMenu(const QPoint& pos);
    void applyFilters();
    QTableWidget* m_table;
    Profile* m_profile = nullptr;
    bool m_hideInstalled = false;
    bool m_hideNotInstalled = false;
    QHash<QString,int> m_activeRows; // fileName -> table row for in-progress downloads
};
}
