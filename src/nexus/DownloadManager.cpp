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

    QDir().mkpath(destDir);

    // Nexus CDN URLs can contain literal spaces; percent-encode robustly.
    QUrl u(QString::fromLatin1(QUrl(url).toEncoded()));
    QNetworkRequest req{u};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Solero/1.0");

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, fileName](qint64 r, qint64 t) {
        emit progress(fileName, r, t);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, fileName, destDir] {
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(fileName, "", false, reply->errorString());
        } else {
            QString dest = destDir + "/" + fileName;
            QFile f(dest);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(reply->readAll());
                f.close();
                emit finished(fileName, dest, true, "");
            } else {
                emit finished(fileName, "", false, "Could not open file for writing: " + dest);
            }
        }
        reply->deleteLater();
    });
}

}
