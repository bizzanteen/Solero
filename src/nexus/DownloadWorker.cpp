#include "DownloadWorker.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace solero {

DownloadWorker::DownloadWorker(QObject* parent) : QObject(parent) {}

void DownloadWorker::ensureNam() {
    if (!m_nam) m_nam = new QNetworkAccessManager(this); // created on the worker thread
}

void DownloadWorker::enqueue(const QString& url, const QString& fileName, const QString& destDir) {
    m_queue.enqueue(Item{fileName, url, destDir});
    if (!m_busy) startNext();
}

void DownloadWorker::startNext() {
    if (m_busy || m_queue.isEmpty()) return;
    ensureNam();
    m_active = m_queue.dequeue();
    m_busy = true;

    QDir().mkpath(m_active.destDir);
    const QString dest = m_active.destDir + "/" + m_active.fileName;
    m_partPath = dest + ".part";

    m_file = new QFile(m_partPath);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString fn = m_active.fileName;
        delete m_file; m_file = nullptr;
        m_busy = false;
        emit finished(fn, "", false, "The download could not start - Solero could not create the output file. "
                                     "Check that the downloads folder is writable.");
        startNext();
        return;
    }

    QUrl u(QString::fromLatin1(QUrl(m_active.url).toEncoded()));
    QNetworkRequest req{u};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Solero/1.0");
    req.setTransferTimeout(60000); // fail a dead mirror instead of hanging forever

    QNetworkReply* reply = m_nam->get(req);
    reply->setReadBufferSize(0); // unbounded: drain the socket as fast as it delivers
    m_reply = reply;
    const QString fileName = m_active.fileName;
    const QString destDir = m_active.destDir;

    connect(reply, &QNetworkReply::downloadProgress, this, [this, fileName](qint64 r, qint64 t) {
        emit progress(fileName, r, t);
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (m_file && m_reply == reply) m_file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, fileName, destDir] {
        if (m_reply != reply) { reply->deleteLater(); return; }

        if (reply->error() != QNetworkReply::NoError) {
            const QString err = reply->errorString();
            cleanupActive();
            m_busy = false;
            emit finished(fileName, "", false, err);
            startNext();
            return;
        }

        if (m_file) {
            m_file->write(reply->readAll());
            m_file->close();
            delete m_file; m_file = nullptr;
        }
        reply->deleteLater();
        m_reply = nullptr;

        const QString dest = destDir + "/" + fileName;
        QFile::remove(dest);
        const QString partPath = m_partPath;
        m_partPath.clear();
        m_busy = false;
        if (QFile::rename(partPath, dest)) {
            emit finished(fileName, dest, true, "");
        } else {
            QFile::remove(partPath);
            emit finished(fileName, "", false, "The download finished but could not be saved to disk. "
                                               "Check that the downloads folder is writable and has enough free space.");
        }
        startNext();
    });
}

void DownloadWorker::cleanupActive() {
    if (m_file) { m_file->close(); delete m_file; m_file = nullptr; }
    if (!m_partPath.isEmpty()) { QFile::remove(m_partPath); m_partPath.clear(); }
    if (m_reply) { m_reply->deleteLater(); m_reply = nullptr; }
}

void DownloadWorker::shutdown() {
    m_queue.clear();
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;        // abort-triggered finished slot becomes a no-op
        r->abort();
        r->deleteLater();
    }
    if (m_file) { m_file->close(); delete m_file; m_file = nullptr; }
    if (!m_partPath.isEmpty()) { QFile::remove(m_partPath); m_partPath.clear(); }
    m_busy = false;
}

void DownloadWorker::cancel(const QString& fileName) {
    if (m_busy && m_active.fileName == fileName) {
        if (m_reply) {
            QNetworkReply* r = m_reply;
            m_reply = nullptr;
            r->abort();
            r->deleteLater();
        }
        if (m_file) { m_file->close(); delete m_file; m_file = nullptr; }
        if (!m_partPath.isEmpty()) { QFile::remove(m_partPath); m_partPath.clear(); }
        m_busy = false;
        emit finished(fileName, "", false, "cancelled");
        startNext();
        return;
    }
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).fileName == fileName) {
            m_queue.removeAt(i);
            emit finished(fileName, "", false, "cancelled");
            return;
        }
    }
}

} // namespace solero
