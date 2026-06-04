#include "DownloadManager.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace solero {

DownloadManager::DownloadManager(QObject* parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
}

void DownloadManager::enqueue(const QString& url, const QString& fileNameIn, const QString& destDir) {
    // sanitize the filename: strip any path separators.
    QString fileName = QFileInfo(fileNameIn).fileName();
    if (fileName.isEmpty()) fileName = QUrl(url).fileName();
    if (fileName.isEmpty()) fileName = "download.bin";

    m_queue.enqueue(Item{fileName, url, destDir});
    if (!m_busy) startNext();
}

void DownloadManager::startNext() {
    if (m_busy || m_queue.isEmpty()) return;
    m_active = m_queue.dequeue();
    m_busy = true;

    QDir().mkpath(m_active.destDir);
    const QString dest = m_active.destDir + "/" + m_active.fileName;
    m_partPath = dest + ".part";

    // Open the .part file up front; stream chunks into it as they arrive.
    m_file = new QFile(m_partPath);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString fn = m_active.fileName;
        delete m_file; m_file = nullptr;
        m_busy = false;
        emit finished(fn, "", false, "Could not open file for writing: " + m_partPath);
        startNext();
        return;
    }

    // Nexus CDN URLs can contain literal spaces; percent-encode robustly.
    QUrl u(QString::fromLatin1(QUrl(m_active.url).toEncoded()));
    QNetworkRequest req{u};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Solero/1.0");

    QNetworkReply* reply = m_nam->get(req);
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
        // cancel() detaches and tears down the reply itself; ignore the stale slot.
        if (m_reply != reply) { reply->deleteLater(); return; }

        if (reply->error() != QNetworkReply::NoError) {
            const QString err = reply->errorString();
            cleanupActive();             // closes + removes .part, deletes reply
            m_busy = false;
            emit finished(fileName, "", false, err);
            startNext();
            return;
        }

        // Flush any trailing bytes and finalize: rename .part -> final.
        if (m_file) {
            m_file->write(reply->readAll());
            m_file->close();
            delete m_file; m_file = nullptr;
        }
        reply->deleteLater();
        m_reply = nullptr;

        const QString dest = destDir + "/" + fileName;
        QFile::remove(dest);                 // overwrite any stale final
        const QString partPath = m_partPath;
        m_partPath.clear();
        m_busy = false;
        if (QFile::rename(partPath, dest)) {
            emit finished(fileName, dest, true, "");
        } else {
            QFile::remove(partPath);
            emit finished(fileName, "", false, "Could not finalize download: " + dest);
        }
        startNext();
    });
}

// Closes/removes the active .part file and tears down the reply.
void DownloadManager::cleanupActive() {
    if (m_file) { m_file->close(); delete m_file; m_file = nullptr; }
    if (!m_partPath.isEmpty()) { QFile::remove(m_partPath); m_partPath.clear(); }
    if (m_reply) { m_reply->deleteLater(); m_reply = nullptr; }
}

void DownloadManager::cancel(const QString& fileName) {
    // Cancel the active download if it matches.
    if (m_busy && m_active.fileName == fileName) {
        // Detach the reply first so its finished slot (fired by abort) no-ops,
        // then abort + clean up the .part file.
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
    // Otherwise drop it from the pending queue.
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).fileName == fileName) {
            m_queue.removeAt(i);
            emit finished(fileName, "", false, "cancelled");
            return;
        }
    }
}

}
