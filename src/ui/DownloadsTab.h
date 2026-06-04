#pragma once
#include <QWidget>
class QListWidget;
namespace solero {
class DownloadsTab : public QWidget {
    Q_OBJECT
public:
    explicit DownloadsTab(QWidget* parent = nullptr);
    void refresh(); // re-list archives in AppConfig downloadsDir
signals:
    void installRequested(const QString& archivePath);
private:
    QListWidget* m_list;
};
}
