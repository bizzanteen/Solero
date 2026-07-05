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
    // Pause the active download: abort the reply but KEEP the .part file. The item
    // stays "busy" (no queued download starts) until resume() re-issues it with an
    // HTTP Range header that appends to the existing .part.
    void pause(const QString& fileName);
    void resume(const QString& fileName);
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
    // Issue the GET for the current m_active. When resuming, opens the .part in
    // read/write (append) mode and sends "Range: bytes=<baseOffset>-".
    void beginRequest(bool resume);
    void cleanupActive();

    QNetworkAccessManager* m_nam = nullptr;
    QQueue<Item> m_queue;
    bool m_busy = false;
    Item m_active;
    QNetworkReply* m_reply = nullptr;
    QFile* m_file = nullptr;
    QString m_partPath;
    bool m_writeFailed = false; // a write()/flush() failed mid-transfer: don't promote the .part
    bool m_paused = false;      // active item is held mid-transfer awaiting resume()
    // Bytes already on disk that the current reply continues from (0 for a fresh
    // start or a server that ignored Range and restarted with a full 200 body).
    // Used to offset progress reporting and the completeness check.
    qint64 m_baseOffset = 0;
    // On a resumed request the 206-vs-200 decision is deferred until the first
    // body bytes arrive, so we don't append a full 200 body onto the partial file.
    bool m_resumeCheckPending = false;
};

} // namespace solero
