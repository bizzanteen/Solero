#pragma once
#include <QObject>
#include <QString>
class QNetworkAccessManager;
namespace solero {
class DownloadManager : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(QObject* parent = nullptr);
    void enqueue(const QString& url, const QString& fileName, const QString& destDir);
signals:
    void progress(const QString& fileName, qint64 received, qint64 total);
    void finished(const QString& fileName, const QString& destPath, bool ok, const QString& error);
private:
    QNetworkAccessManager* m_nam;
};
}
