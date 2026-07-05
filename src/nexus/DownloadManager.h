#pragma once
#include <QObject>
#include <QString>
class QThread;

namespace solero {
class DownloadWorker;

// Public-facing download facade. Owns a worker thread that performs the actual
// transfer; enqueue/cancel are forwarded to the worker via queued signals, and
// the worker's progress/finished are re-emitted on the GUI thread. The public
// API is unchanged from the original single-threaded version.
class DownloadManager : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(QObject* parent = nullptr);
    ~DownloadManager() override;
    void enqueue(const QString& url, const QString& fileName, const QString& destDir);
    void cancel(const QString& fileName);
    // Pause/resume an in-progress download (HTTP Range resume; see DownloadWorker).
    void pause(const QString& fileName);
    void resume(const QString& fileName);
signals:
    void progress(const QString& fileName, qint64 received, qint64 total);
    void finished(const QString& fileName, const QString& destPath, bool ok, const QString& error);
    // Internal: forwarded to the worker thread (queued connections).
    void requestEnqueue(const QString& url, const QString& fileName, const QString& destDir);
    void requestCancel(const QString& fileName);
    void requestPause(const QString& fileName);
    void requestResume(const QString& fileName);
private:
    QThread* m_thread = nullptr;
    DownloadWorker* m_worker = nullptr;
};
}
