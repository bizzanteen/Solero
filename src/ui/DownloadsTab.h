#pragma once
#include <QWidget>
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
signals:
    void installRequested(const QString& archivePath);
private:
    void showContextMenu(const QPoint& pos);
    void applyFilters();
    QTableWidget* m_table;
    Profile* m_profile = nullptr;
    bool m_hideInstalled = false;
    bool m_hideNotInstalled = false;
};
}
