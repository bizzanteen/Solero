#include "DownloadWorker.h"
#include "core/Log.h"
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
    m_paused = false;
    m_baseOffset = 0;

    QDir().mkpath(m_active.destDir);
    const QString dest = m_active.destDir + "/" + m_active.fileName;
    m_partPath = dest + ".part";

    qCInfo(lcDownload) << "download start" << m_active.fileName << "from" << QUrl(m_active.url).host();
    beginRequest(false);
}

void DownloadWorker::beginRequest(bool resume) {
    // Resume appends to an existing .part; a fresh start truncates. When resuming
    // we open read/write (no truncate) so the pre-existing bytes survive, then seek
    // to the end and record m_baseOffset - the point the server should continue from.
    m_writeFailed = false;
    m_baseOffset = 0;
    const bool haveExisting = resume && QFileInfo::exists(m_partPath) && QFileInfo(m_partPath).size() > 0;
    const QIODevice::OpenMode mode = haveExisting
        ? QIODevice::OpenMode(QIODevice::ReadWrite)          // keep bytes, we seek to end
        : QIODevice::OpenMode(QIODevice::WriteOnly | QIODevice::Truncate);

    m_file = new QFile(m_partPath);
    if (!m_file->open(mode)) {
        qCWarning(lcDownload) << "could not open output file" << m_partPath;
        const QString fn = m_active.fileName;
        delete m_file; m_file = nullptr;
        m_busy = false;
        m_partPath.clear();
        emit finished(fn, "", false, "The download could not start - Solero could not create the output file. "
                                     "Check that the downloads folder is writable.");
        startNext();
        return;
    }
    if (haveExisting) {
        m_baseOffset = m_file->size();
        m_file->seek(m_baseOffset);
    }
    // On a resume we only commit to appending once we've confirmed a 206 response;
    // a 200 (server ignored Range) or an error status restarts from byte 0.
    m_resumeCheckPending = (m_baseOffset > 0);

    QUrl u(QString::fromLatin1(QUrl(m_active.url).toEncoded()));
    QNetworkRequest req{u};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Solero/1.0");
    req.setTransferTimeout(60000); // fail a dead mirror instead of hanging forever
    if (m_baseOffset > 0) {
        req.setRawHeader("Range", "bytes=" + QByteArray::number(m_baseOffset) + "-");
        qCInfo(lcDownload) << "resume request" << m_active.fileName << "Range bytes=" << m_baseOffset << "-";
    }

    QNetworkReply* reply = m_nam->get(req);
    reply->setReadBufferSize(0); // unbounded: drain the socket as fast as it delivers
    m_reply = reply;
    const QString fileName = m_active.fileName;
    const QString destDir = m_active.destDir;

    connect(reply, &QNetworkReply::downloadProgress, this, [this, fileName](qint64 r, qint64 t) {
        // r/t are for this reply's body; on a 206 resume they cover only the tail, so
        // offset by the bytes already on disk to report whole-file progress.
        const qint64 recv = m_baseOffset + r;
        const qint64 tot  = (t > 0 && m_baseOffset > 0) ? m_baseOffset + t : t;
        qCDebug(lcDownload) << "progress" << fileName << recv << "/" << tot;
        emit progress(fileName, recv, tot);
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        // Check every write: a short write means the disk is full or the
        // file went away. Stop the transfer and let finished() delete the .part.
        if (!(m_file && m_reply == reply && !m_writeFailed)) return;

        // First body bytes of a resume: decide 206 (append) vs 200 (restart) before
        // writing, so a full 200 body is never appended onto the partial file.
        if (m_resumeCheckPending) {
            m_resumeCheckPending = false;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 206) {
                qCInfo(lcDownload) << "resume honored (206) at offset" << m_baseOffset << m_active.fileName;
            } else {
                qCInfo(lcDownload) << "resume not honored (HTTP" << status << ") - restarting clean" << m_active.fileName;
                m_file->seek(0);
                if (!m_file->resize(0)) {
                    m_writeFailed = true;
                    qCWarning(lcDownload) << "restart truncate failed:" << m_file->errorString();
                    reply->abort();
                    return;
                }
                m_baseOffset = 0; // full body now written from byte 0
            }
        }

        const QByteArray chunk = reply->readAll();
        if (m_file->write(chunk) != chunk.size()) {
            m_writeFailed = true;
            qCWarning(lcDownload) << "write failed:" << m_file->errorString();
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, fileName, destDir] {
        if (m_reply != reply) { reply->deleteLater(); return; }

        // Disk-write failure detected mid-transfer. The .part is truncated,
        // so fail rather than promote it. Checked before error() so the abort we
        // issued from readyRead isn't misreported as a network cancellation.
        if (m_writeFailed) {
            qCWarning(lcDownload) << "download failed" << fileName << ": could not write to disk";
            cleanupActive();
            m_busy = false;
            emit finished(fileName, "", false,
                          "The download could not be saved - writing to disk failed. "
                          "Check that the downloads folder has enough free space.");
            startNext();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            const QString err = reply->errorString();
            qCWarning(lcDownload) << "download failed" << fileName << ":" << err;
            cleanupActive();
            m_busy = false;
            emit finished(fileName, "", false, err);
            startNext();
            return;
        }

        // Server-authoritative expected size; 0 when the server sends no
        // Content-Length (e.g. chunked), in which case the size check is skipped.
        // On a 206 resume the Content-Length covers only the tail we requested, so
        // add the pre-existing bytes back to get the whole-file expected size.
        const qint64 clen = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        const qint64 expected = (clen > 0 && m_baseOffset > 0) ? m_baseOffset + clen : clen;

        // Flush the tail and close, checking every write/close.
        bool diskOk = true;
        if (m_file) {
            const QByteArray tail = reply->readAll();
            if (m_file->write(tail) != tail.size() || !m_file->flush()) {
                diskOk = false;
                qCWarning(lcDownload) << "final write failed:" << m_file->errorString();
            }
            m_file->close();
            if (m_file->error() != QFileDevice::NoError) {
                diskOk = false;
                qCWarning(lcDownload) << "close failed:" << m_file->errorString();
            }
            delete m_file; m_file = nullptr;
        }
        reply->deleteLater();
        m_reply = nullptr;

        const QString partPath = m_partPath;
        m_partPath.clear();
        m_busy = false;

        if (!diskOk) {
            QFile::remove(partPath);
            emit finished(fileName, "", false,
                          "The download could not be saved to disk. "
                          "Check that the downloads folder is writable and has enough free space.");
            startNext();
            return;
        }

        // Verify completeness against Content-Length: a truncated transfer
        // (dropped mirror, short body) must not be promoted to a file the installer
        // would treat as valid.
        const qint64 got = QFileInfo(partPath).size();
        if (expected > 0 && got != expected) {
            qCWarning(lcDownload) << "size mismatch" << fileName << "expected" << expected << "got" << got;
            QFile::remove(partPath);
            emit finished(fileName, "", false,
                          "The download was incomplete - the saved file is a different size than "
                          "the server reported. Please try downloading it again.");
            startNext();
            return;
        }

        const QString dest = destDir + "/" + fileName;
        QFile::remove(dest);
        if (QFile::rename(partPath, dest)) {
            qCInfo(lcDownload) << "download finished" << fileName << "ok" << got << "bytes";
            emit finished(fileName, dest, true, "");
        } else {
            qCWarning(lcDownload) << "rename to final failed" << partPath << "->" << dest;
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
    m_paused = false;
    m_resumeCheckPending = false;
    m_baseOffset = 0;
}

void DownloadWorker::pause(const QString& fileName) {
    // Only the in-flight (non-paused) active download can be paused.
    if (!(m_busy && !m_paused && m_active.fileName == fileName)) return;
    qCInfo(lcDownload) << "pause requested" << fileName;
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;   // this reply's finished slot becomes a no-op
        r->abort();
        r->deleteLater();
    }
    // Flush + close but KEEP the .part on disk; resume() re-opens and appends to it.
    if (m_file) {
        m_file->flush();
        m_file->close();
        delete m_file; m_file = nullptr;
    }
    m_resumeCheckPending = false;
    m_paused = true; // m_busy stays true + m_partPath retained until resume/cancel
}

void DownloadWorker::resume(const QString& fileName) {
    if (!(m_busy && m_paused && m_active.fileName == fileName)) return;
    const qint64 have = QFileInfo::exists(m_partPath) ? QFileInfo(m_partPath).size() : 0;
    qCInfo(lcDownload) << "resume requested" << fileName << "have" << have << "bytes on disk";
    m_paused = false;
    ensureNam();
    beginRequest(true);
}

void DownloadWorker::cancel(const QString& fileName) {
    qCInfo(lcDownload) << "cancel requested" << fileName;
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
        m_paused = false;      // also covers cancelling a paused (held) download
        m_resumeCheckPending = false;
        m_baseOffset = 0;
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
