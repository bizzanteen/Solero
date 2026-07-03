#include "NexusBrowser.h"
#include "ProgressModal.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QTextBrowser>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QPixmap>
#include <QIcon>
#include <QStyle>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QApplication>
#include <QPointer>

using solero::NexusBrowser;

namespace {

QString humanSize(qint64 sizeKb) {
    if (sizeKb <= 0) return {};
    double kb = static_cast<double>(sizeKb);
    if (kb < 1024.0) return QString::number(kb, 'f', 0) + " KB";
    double mb = kb / 1024.0;
    if (mb < 1024.0) return QString::number(mb, 'f', 1) + " MB";
    return QString::number(mb / 1024.0, 'f', 2) + " GB";
}

// Minimal BBCode -> plain text: drop every [tag]. Decode a couple of common
// entities so the description reads naturally.
QString stripBBCode(QString s) {
    s.remove(QRegularExpression("\\[[^\\]]*\\]"));
    s.replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&").replace("&quot;", "\"");
    return s;
}

} // namespace

NexusBrowser::NexusBrowser(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Browse Nexus");
    setWindowFlag(Qt::Window, true);
    resize(920, 660);
    setModal(false);

    m_nam = new QNetworkAccessManager(this);

    m_stack = new QStackedWidget(this);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_stack);

    // Page 0: results
    auto* resultsPage = new QWidget(m_stack);
    auto* rl = new QVBoxLayout(resultsPage);

    auto* topBar = new QHBoxLayout();
    m_search = new QLineEdit(resultsPage);
    m_search->setPlaceholderText("Search Nexus mods\xe2\x80\xa6");
    m_search->setClearButtonEnabled(true);
    auto* searchBtn = new QPushButton("Search", resultsPage);
    auto* trendingBtn = new QPushButton("Trending", resultsPage);
    auto* latestBtn = new QPushButton("Latest", resultsPage);
    auto* updatedBtn = new QPushButton("Updated", resultsPage);
    topBar->addWidget(m_search, 1);
    topBar->addWidget(searchBtn);
    topBar->addSpacing(8);
    topBar->addWidget(trendingBtn);
    topBar->addWidget(latestBtn);
    topBar->addWidget(updatedBtn);
    rl->addLayout(topBar);

    m_status = new QLabel(resultsPage);
    m_status->setStyleSheet("color: gray;");
    rl->addWidget(m_status);

    m_results = new QListWidget(resultsPage);
    m_results->setIconSize(QSize(64, 64));
    m_results->setUniformItemSizes(false);
    m_results->setWordWrap(true);
    rl->addWidget(m_results, 1);

    m_stack->addWidget(resultsPage);

    // Page 1: mod detail
    auto* detailPage = new QWidget(m_stack);
    auto* dl = new QVBoxLayout(detailPage);

    auto* backBtn = new QPushButton("\xe2\x86\x90 Back", detailPage);
    backBtn->setMaximumWidth(120);
    dl->addWidget(backBtn);

    auto* header = new QHBoxLayout();
    m_detailImage = new QLabel(detailPage);
    m_detailImage->setFixedSize(220, 140);
    m_detailImage->setScaledContents(true);
    m_detailImage->setStyleSheet("background:#222; border:1px solid #444;");
    header->addWidget(m_detailImage);

    auto* headText = new QVBoxLayout();
    m_detailName = new QLabel(detailPage);
    m_detailName->setStyleSheet("font-size:18px; font-weight:bold;");
    m_detailName->setWordWrap(true);
    m_detailMeta = new QLabel(detailPage);
    m_detailMeta->setStyleSheet("color:gray;");
    m_detailSummary = new QLabel(detailPage);
    m_detailSummary->setStyleSheet("font-weight:bold;");
    m_detailSummary->setWordWrap(true);
    headText->addWidget(m_detailName);
    headText->addWidget(m_detailMeta);
    headText->addWidget(m_detailSummary);
    headText->addStretch(1);
    header->addLayout(headText, 1);
    dl->addLayout(header);

    auto* btnRow = new QHBoxLayout();
    auto* endorseBtn = new QPushButton("Endorse", detailPage);
    auto* openBtn = new QPushButton("Open on Nexus", detailPage);
    btnRow->addWidget(endorseBtn);
    btnRow->addWidget(openBtn);
    btnRow->addStretch(1);
    dl->addLayout(btnRow);

    m_detailDesc = new QTextBrowser(detailPage);
    m_detailDesc->setReadOnly(true);
    dl->addWidget(m_detailDesc, 1);

    dl->addWidget(new QLabel("Files:", detailPage));
    m_fileTable = new QTableWidget(detailPage);
    m_fileTable->setColumnCount(4);
    m_fileTable->setHorizontalHeaderLabels({"Name", "Version", "Size", ""});
    m_fileTable->horizontalHeader()->setStretchLastSection(false);
    m_fileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileTable->verticalHeader()->setVisible(false);
    m_fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dl->addWidget(m_fileTable, 1);

    m_stack->addWidget(detailPage);

    // Wiring
    connect(searchBtn, &QPushButton::clicked, this, &NexusBrowser::doSearch);
    connect(m_search, &QLineEdit::returnPressed, this, &NexusBrowser::doSearch);
    connect(trendingBtn, &QPushButton::clicked, this, [this]{ showList("trending"); });
    connect(latestBtn, &QPushButton::clicked, this, [this]{ showList("latest"); });
    connect(updatedBtn, &QPushButton::clicked, this, [this]{ showList("updated"); });

    connect(m_results, &QListWidget::itemActivated, this, [this](QListWidgetItem* it){
        if (it) openMod(it->data(Qt::UserRole).toString());
    });
    connect(m_results, &QListWidget::itemClicked, this, [this](QListWidgetItem* it){
        if (it) openMod(it->data(Qt::UserRole).toString());
    });

    connect(backBtn, &QPushButton::clicked, this, [this]{ m_stack->setCurrentIndex(0); });
    connect(openBtn, &QPushButton::clicked, this, [this]{
        if (!m_currentModId.isEmpty())
            QDesktopServices::openUrl(QUrl(
                "https://www.nexusmods.com/skyrimspecialedition/mods/" + m_currentModId));
    });
    connect(endorseBtn, &QPushButton::clicked, this, [this]{
        if (m_currentModId.isEmpty()) return;
        auto res = NexusApi::endorse(m_currentModId, m_currentVersion, false);
        if (res.ok)
            QMessageBox::information(this, "Endorse", "Endorsed. Thank you!");
        else
            QMessageBox::warning(this, "Endorse",
                res.message.isEmpty() ? QString("The endorsement could not be submitted - Nexus did not return a result. Try again from the mod page.") : res.message);
    });

    showList("trending");
}

void NexusBrowser::doSearch() {
    const QString q = m_search->text().trimmed();
    if (q.isEmpty()) return;
    m_status->setText("Searching\xe2\x80\xa6");
    m_results->setEnabled(false);
    qApp->processEvents();
    auto rows = NexusApi::search(q);
    m_results->setEnabled(true);
    populateResults(rows, "No results for \"" + q + "\".");
}

void NexusBrowser::showList(const QString& which) {
    QString label = which == "trending" ? "trending"
                  : which == "latest"   ? "latest added" : "latest updated";
    m_status->setText("Loading " + label + "\xe2\x80\xa6");
    m_results->setEnabled(false);
    qApp->processEvents();
    QList<NexusApi::ModSummary> rows;
    if (which == "trending")      rows = NexusApi::trending();
    else if (which == "latest")   rows = NexusApi::latestAdded();
    else                          rows = NexusApi::latestUpdated();
    m_results->setEnabled(true);
    populateResults(rows, "No " + label + " mods found.");
    if (!rows.isEmpty())
        m_status->setText("Showing " + label + " (" + QString::number(rows.size()) + ")");
}

void NexusBrowser::populateResults(const QList<NexusApi::ModSummary>& rows, const QString& emptyMsg) {
    ++m_gen; // invalidate any in-flight thumbnail fetches
    m_results->clear();
    if (rows.isEmpty()) {
        m_status->setText(emptyMsg);
        return;
    }
    m_status->clear();

    QIcon placeholder = style()->standardIcon(QStyle::SP_FileIcon);
    for (const auto& m : rows) {
        QString summary = m.summary;
        if (summary.size() > 160) summary = summary.left(157) + "\xe2\x80\xa6";
        QString text = m.name;
        if (m.adult) text += "  [18+]";
        text += "\n  by " + (m.author.isEmpty() ? QString("unknown") : m.author)
              + " - " + QString::number(m.endorsements) + " endorsements";
        if (!summary.isEmpty()) text += "\n  " + summary;

        auto* item = new QListWidgetItem(placeholder, text, m_results);
        item->setData(Qt::UserRole, m.modId);
        if (!m.pictureUrl.isEmpty())
            fetchImageInto(item, m.pictureUrl, m.modId);
    }
}

void NexusBrowser::fetchImageInto(QListWidgetItem* item, const QString& url, const QString& modId) {
    const quint64 gen = m_gen;
    QPointer<QListWidget> list = m_results;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "Solero/1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, list, item]{
        reply->deleteLater();
        if (gen != m_gen || !list) return;            // list was repopulated
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        if (!pm.loadFromData(reply->readAll())) return;
        // Confirm the item still belongs to the current list before touching it.
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i) == item) {
                item->setIcon(QIcon(pm.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                return;
            }
    });
}

void NexusBrowser::fetchHeaderImage(const QString& url) {
    if (url.isEmpty()) { m_detailImage->clear(); return; }
    const quint64 gen = m_gen;
    QString modId = m_currentModId;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "Solero/1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, modId]{
        reply->deleteLater();
        if (gen != m_gen || modId != m_currentModId) return; // navigated away
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        if (pm.loadFromData(reply->readAll())) m_detailImage->setPixmap(pm);
    });
}

void NexusBrowser::openMod(const QString& modId) {
    if (modId.isEmpty()) return;
    ++m_gen; // invalidate result-thumb fetches; tag header fetch with new gen
    m_currentModId = modId;

    ProgressModal prog(this, "Nexus", "Loading mod\xe2\x80\xa6");
    prog.show(); prog.pump();

    auto d = NexusApi::modDetails(modId);
    auto files = NexusApi::files(modId);

    if (!d.ok) {
        QMessageBox::warning(this, "Nexus", "Could not load this mod's details from Nexus. Check your connection or try refreshing.");
        return;
    }

    m_currentVersion = d.version;
    m_detailName->setText(d.name);
    m_detailMeta->setText(QString("by %1  \xe2\x80\xa2  v%2  \xe2\x80\xa2  %3 endorsements%4")
        .arg(d.author.isEmpty() ? "unknown" : d.author,
             d.version.isEmpty() ? "?" : d.version)
        .arg(d.endorsements)
        .arg(d.adult ? "  [18+]" : ""));
    m_detailSummary->setText(d.summary);
    m_detailSummary->setVisible(!d.summary.isEmpty());
    m_detailDesc->setPlainText(stripBBCode(d.description));
    m_detailImage->clear();
    fetchHeaderImage(d.pictureUrl);

    // File list.
    m_fileTable->setRowCount(files.size());
    for (int i = 0; i < files.size(); ++i) {
        const auto& f = files[i];
        auto* nameItem = new QTableWidgetItem(f.name);
        nameItem->setData(Qt::UserRole, f.fileId);
        nameItem->setData(Qt::UserRole + 1, f.version.isEmpty() ? d.version : f.version);
        m_fileTable->setItem(i, 0, nameItem);
        m_fileTable->setItem(i, 1, new QTableWidgetItem(f.version));
        m_fileTable->setItem(i, 2, new QTableWidgetItem(humanSize(f.sizeKb)));
        auto* dlBtn = new QPushButton("Download", m_fileTable);
        connect(dlBtn, &QPushButton::clicked, this, [this, i]{ onDownloadFile(i); });
        m_fileTable->setCellWidget(i, 3, dlBtn);
    }
    m_fileTable->resizeColumnToContents(1);
    m_fileTable->resizeColumnToContents(2);
    m_fileTable->resizeColumnToContents(3);

    m_stack->setCurrentIndex(1);
}

void NexusBrowser::onDownloadFile(int row) {
    auto* nameItem = m_fileTable->item(row, 0);
    if (!nameItem) return;
    const QString fileId = nameItem->data(Qt::UserRole).toString();
    const QString fileName = nameItem->text();
    const QString version = nameItem->data(Qt::UserRole + 1).toString();
    emit downloadRequested(m_currentModId, fileId, fileName, version);
}
