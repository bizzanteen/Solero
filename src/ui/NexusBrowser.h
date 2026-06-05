#pragma once
#include <QDialog>
#include <QString>
#include <QList>
#include "nexus/NexusApi.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QLabel;
class QTextBrowser;
class QTableWidget;
class QNetworkAccessManager;

namespace solero {

// Non-modal in-app Nexus browser: search + curated lists on a results page,
// and a mod-detail page (image, description, file list) with one-click
// download + endorse. Reuses the static NexusApi (synchronous curl) for data;
// image thumbnails are fetched asynchronously via a shared QNetworkAccessManager.
// Downloads are delegated to MainWindow via the downloadRequested signal.
class NexusBrowser : public QDialog {
    Q_OBJECT
public:
    explicit NexusBrowser(QWidget* parent = nullptr);

signals:
    void downloadRequested(const QString& modId, const QString& fileId,
                           const QString& fileName, const QString& version);

private:
    void doSearch();
    void showList(const QString& which);   // "trending"/"latest"/"updated"
    void populateResults(const QList<NexusApi::ModSummary>& rows, const QString& emptyMsg);
    void openMod(const QString& modId);
    void fetchImageInto(QListWidgetItem* item, const QString& url, const QString& modId);
    void fetchHeaderImage(const QString& url);
    void onDownloadFile(int row);

    QStackedWidget* m_stack = nullptr;

    // Results page.
    QLineEdit*   m_search = nullptr;
    QListWidget* m_results = nullptr;
    QLabel*      m_status = nullptr;

    // Detail page.
    QLabel*       m_detailImage = nullptr;
    QLabel*       m_detailName = nullptr;
    QLabel*       m_detailMeta = nullptr;
    QLabel*       m_detailSummary = nullptr;
    QTextBrowser* m_detailDesc = nullptr;
    QTableWidget* m_fileTable = nullptr;
    QString       m_currentModId;
    QString       m_currentVersion;

    QNetworkAccessManager* m_nam = nullptr;
    // Generation counter: bumped on every repopulate so stale image replies are ignored.
    quint64 m_gen = 0;
};

} // namespace solero
