#pragma once
#include <QObject>
#include <QString>
#include <QQueue>
class QNetworkAccessManager;
class QNetworkReply;
class QFile;
namespace solero {
class DownloadManager : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(QObject* parent = nullptr);
    void enqueue(const QString& url, const QString& fileName, const QString& destDir);
    // Cancel the active download (or remove a pending one) matching fileName.
    void cancel(const QString& fileName);
signals:
    void progress(const QString& fileName, qint64 received, qint64 total);
    void finished(const QString& fileName, const QString& destPath, bool ok, const QString& error);
private:
    struct Item { QString fileName, url, destDir; };
    void startNext();
    void cleanupActive();

    QNetworkAccessManager* m_nam;
    QQueue<Item> m_queue;
    bool m_busy = false;
    // Active download state.
    Item m_active;
    QNetworkReply* m_reply = nullptr;
    QFile* m_file = nullptr;
    QString m_partPath;
};
}
