#include "DownloadManager.h"
#include "DownloadWorker.h"
#include <QThread>
#include <QUrl>
#include <QFileInfo>

namespace solero {

DownloadManager::DownloadManager(QObject* parent) : QObject(parent) {
    m_thread = new QThread(this);
    m_worker = new DownloadWorker;            // no parent; lives on m_thread
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &DownloadManager::requestEnqueue, m_worker, &DownloadWorker::enqueue);
    connect(this, &DownloadManager::requestCancel,  m_worker, &DownloadWorker::cancel);
    connect(m_worker, &DownloadWorker::progress, this, &DownloadManager::progress);
    connect(m_worker, &DownloadWorker::finished, this, &DownloadManager::finished);
    m_thread->start();
}

DownloadManager::~DownloadManager() {
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
}

void DownloadManager::enqueue(const QString& url, const QString& fileNameIn, const QString& destDir) {
    // Sanitize the filename on the GUI thread (pure string work), then forward.
    QString fileName = QFileInfo(fileNameIn).fileName();
    if (fileName.isEmpty()) fileName = QUrl(url).fileName();
    if (fileName.isEmpty()) fileName = "download.bin";
    emit requestEnqueue(url, fileName, destDir);
}

void DownloadManager::cancel(const QString& fileName) {
    emit requestCancel(fileName);
}

} // namespace solero
