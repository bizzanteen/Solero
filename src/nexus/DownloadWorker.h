#pragma once
#include <QObject>
#include <QString>
#include <QQueue>
class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace solero {

// Runs on a dedicated thread (see DownloadManager). Owns its own
// QNetworkAccessManager so byte transfer + file writes are never stalled by the
// GUI thread's blocking event loops. Processes one download at a time.
class DownloadWorker : public QObject {
    Q_OBJECT
public:
    explicit DownloadWorker(QObject* parent = nullptr);

public slots:
    void enqueue(const QString& url, const QString& fileName, const QString& destDir);
    void cancel(const QString& fileName);
    // Synchronously abort any active download and clear the queue. Called via a
    // BlockingQueuedConnection from DownloadManager's destructor before the
    // worker thread stops, so no .part file or socket is leaked at shutdown.
    void shutdown();

signals:
    void progress(const QString& fileName, qint64 received, qint64 total);
    void finished(const QString& fileName, const QString& destPath, bool ok, const QString& error);

private:
    struct Item { QString fileName, url, destDir; };
    void ensureNam();
    void startNext();
    void cleanupActive();

    QNetworkAccessManager* m_nam = nullptr;
    QQueue<Item> m_queue;
    bool m_busy = false;
    Item m_active;
    QNetworkReply* m_reply = nullptr;
    QFile* m_file = nullptr;
    QString m_partPath;
    bool m_writeFailed = false; // a write()/flush() failed mid-transfer: don't promote the .part
};

} // namespace solero
