#include "WabbajackDialog.h"
#include "wabbajack/WabbajackEngine.h"
#include "import/Mo2Importer.h"
#include "nexus/NexusApi.h"
#include "core/AppConfig.h"

#include <QStackedWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QMenu>
#include <QAction>
#include <QPushButton>
#include <QListWidget>
#include <QLabel>
#include <QTextBrowser>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSplitter>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QScrollArea>
#include <QGroupBox>
#include <QListWidget>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSettings>
#include <QPixmap>
#include <QIcon>
#include <QApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCloseEvent>

namespace solero {

WabbajackDialog::WabbajackDialog(ProfileManager* profiles, QWidget* parent)
    : QDialog(parent), m_profiles(profiles) {
    setWindowTitle("Install Wabbajack Modlist");
    setModal(true);
    resize(960, 640);

    m_engine = new WabbajackEngine(this);
    m_net = new QNetworkAccessManager(this);

    connect(m_engine, &WabbajackEngine::modlistsReady, this, &WabbajackDialog::onModlistsReady);
    connect(m_engine, &WabbajackEngine::failed, this, &WabbajackDialog::onFailed);
    connect(m_engine, &WabbajackEngine::progress, this, &WabbajackDialog::onProgress);
    connect(m_engine, &WabbajackEngine::logLine, this, &WabbajackDialog::onLogLine);
    connect(m_engine, &WabbajackEngine::installFinished, this, &WabbajackDialog::onInstallFinished);
    connect(m_engine, &WabbajackEngine::installFailed, this, &WabbajackDialog::onInstallFailed);

    auto* layout = new QVBoxLayout(this);
    m_stack = new QStackedWidget(this);
    layout->addWidget(m_stack);

    buildGalleryPage();
    buildProgressPage();

    if (!WabbajackEngine::available()) {
        showEngineMissing();
    } else {
        startFetch();
    }
}

void WabbajackDialog::buildGalleryPage() {
    m_galleryPage = new QWidget(this);
    auto* outer = new QVBoxLayout(m_galleryPage);

    // Top bar
    auto* topBar = new QHBoxLayout;
    m_search = new QLineEdit(m_galleryPage);
    m_search->setPlaceholderText("Search modlists\xe2\x80\xa6");
    m_search->setClearButtonEnabled(true);
    topBar->addWidget(m_search, 1);

    // Official / Unofficial filter.
    m_officialCombo = new QComboBox(m_galleryPage);
    m_officialCombo->addItem("All lists",   int(OfficialFilter::All));
    m_officialCombo->addItem("Official",    int(OfficialFilter::Official));
    m_officialCombo->addItem("Unofficial",  int(OfficialFilter::Unofficial));
    topBar->addWidget(m_officialCombo);

    // Tag filter - a dropdown menu of checkable tags (populated on fetch).
    m_tagBtn = new QPushButton("Tags", m_galleryPage);
    m_tagMenu = new QMenu(m_tagBtn);
    m_tagBtn->setMenu(m_tagMenu);
    topBar->addWidget(m_tagBtn);

    // Solero targets Skyrim SE only - the gallery is filtered to it (no game switcher).
    m_refreshBtn = new QPushButton("Refresh", m_galleryPage);
    topBar->addWidget(m_refreshBtn);

    auto* fileBtn = new QPushButton("Install from .wabbajack file\xe2\x80\xa6", m_galleryPage);
    topBar->addWidget(fileBtn);
    outer->addLayout(topBar);

    // Status placeholder (loading / error)
    auto* statusRow = new QHBoxLayout;
    m_statusLabel = new QLabel(m_galleryPage);
    m_statusLabel->setWordWrap(true);
    statusRow->addWidget(m_statusLabel, 1);
    m_retryBtn = new QPushButton("Retry", m_galleryPage);
    m_retryBtn->setVisible(false);
    statusRow->addWidget(m_retryBtn);
    outer->addLayout(statusRow);

    // Main split: list + details
    auto* split = new QSplitter(Qt::Horizontal, m_galleryPage);

    m_list = new QListWidget(split);
    m_list->setIconSize(QSize(80, 45));
    m_list->setUniformItemSizes(false);
    split->addWidget(m_list);

    auto* details = new QWidget(split);
    auto* dl = new QVBoxLayout(details);
    m_detailImage = new QLabel(details);
    m_detailImage->setMinimumHeight(160);
    m_detailImage->setAlignment(Qt::AlignCenter);
    m_detailImage->setStyleSheet("background:#1a1a1a;");
    dl->addWidget(m_detailImage);

    m_detailTitle = new QLabel(details);
    m_detailTitle->setWordWrap(true);
    m_detailTitle->setStyleSheet("font-size:16px; font-weight:bold;");
    dl->addWidget(m_detailTitle);

    m_detailMeta = new QLabel(details);
    m_detailMeta->setWordWrap(true);
    m_detailMeta->setStyleSheet("color:#aaa;");
    dl->addWidget(m_detailMeta);

    m_detailDesc = new QTextBrowser(details);
    m_detailDesc->setOpenExternalLinks(false);
    dl->addWidget(m_detailDesc, 1);

    auto* detailBtns = new QHBoxLayout;
    m_readmeBtn = new QPushButton("Open readme \xe2\x86\x97", details);
    m_readmeBtn->setEnabled(false);
    detailBtns->addWidget(m_readmeBtn);
    detailBtns->addStretch();
    m_installBtn = new QPushButton("Install", details);
    m_installBtn->setEnabled(false);
    m_installBtn->setStyleSheet("font-weight:bold; padding:6px 18px;");
    detailBtns->addWidget(m_installBtn);
    dl->addLayout(detailBtns);

    split->addWidget(details);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    outer->addWidget(split, 1);

    m_stack->addWidget(m_galleryPage);

    // Connections
    connect(m_search, &QLineEdit::textChanged, this, &WabbajackDialog::applyFilter);
    connect(m_officialCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { applyFilter(); });
    connect(m_refreshBtn, &QPushButton::clicked, this, &WabbajackDialog::startFetch);
    connect(m_retryBtn, &QPushButton::clicked, this, &WabbajackDialog::startFetch);
    connect(m_list, &QListWidget::currentRowChanged, this, &WabbajackDialog::onSelectionChanged);
    connect(m_installBtn, &QPushButton::clicked, this, [this] {
        int row = m_list->currentRow();
        if (row < 0 || row >= m_filtered.size()) return;
        const auto& ml = m_filtered.at(row);
        // install -m needs the namespaced "Author/ListName" id, not the bare machineURL.
        const QString id = ml.namespacedName.isEmpty() ? ml.machineUrl : ml.namespacedName;
        triggerInstall(id, false, ml.title);
    });
    connect(m_readmeBtn, &QPushButton::clicked, this, [this] {
        int row = m_list->currentRow();
        if (row < 0 || row >= m_filtered.size()) return;
        const QString url = m_filtered.at(row).readmeUrl;
        if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url));
    });
    connect(fileBtn, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, "Select a .wabbajack file", QDir::homePath(),
            "Wabbajack modlists (*.wabbajack);;All files (*)");
        if (file.isEmpty()) return;
        triggerInstall(file, true, QFileInfo(file).completeBaseName());
    });
}

void WabbajackDialog::buildProgressPage() {
    m_progressPage = new QWidget(this);
    auto* l = new QVBoxLayout(m_progressPage);

    m_progTitle = new QLabel(m_progressPage);
    m_progTitle->setStyleSheet("font-size:16px; font-weight:bold;");
    m_progTitle->setWordWrap(true);
    l->addWidget(m_progTitle);

    m_progBar = new QProgressBar(m_progressPage);
    m_progBar->setRange(0, 100);
    m_progBar->setValue(0);
    l->addWidget(m_progBar);

    m_progOp = new QLabel(m_progressPage);
    m_progOp->setWordWrap(true);
    m_progOp->setStyleSheet("color:#aaa;");
    l->addWidget(m_progOp);

    m_progLog = new QPlainTextEdit(m_progressPage);
    m_progLog->setReadOnly(true);
    m_progLog->setMaximumBlockCount(5000);
    m_progLog->setStyleSheet("font-family:monospace; font-size:11px;");
    l->addWidget(m_progLog, 1);

    auto* btnRow = new QHBoxLayout;
    m_backBtn = new QPushButton("Back to gallery", m_progressPage);
    m_backBtn->setVisible(false);
    connect(m_backBtn, &QPushButton::clicked, this, [this] {
        m_stack->setCurrentWidget(m_galleryPage);
    });
    btnRow->addWidget(m_backBtn);
    m_retryInstallBtn = new QPushButton("Retry / Resume", m_progressPage);
    m_retryInstallBtn->setVisible(false);
    m_retryInstallBtn->setToolTip("Re-run the install with the same directories "
                                  "- the engine resumes from the downloads folder.");
    connect(m_retryInstallBtn, &QPushButton::clicked, this, [this] {
        if (!m_installTarget.isEmpty()) startInstallRun();
    });
    btnRow->addWidget(m_retryInstallBtn);
    btnRow->addStretch();
    m_cancelBtn = new QPushButton("Cancel", m_progressPage);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this] {
        confirmCloseWhileInstalling();
    });
    btnRow->addWidget(m_cancelBtn);
    l->addLayout(btnRow);

    m_stack->addWidget(m_progressPage);
}

void WabbajackDialog::showEngineMissing() {
    m_list->setVisible(false);
    m_search->setEnabled(false);
    m_refreshBtn->setEnabled(false);
    m_statusLabel->setText(
        "jackify-engine not found. Install Jackify "
        "(github.com/Omni-guides/Jackify) or set the engine path.");
    m_retryBtn->setText("Set engine path\xe2\x80\xa6");
    m_retryBtn->setVisible(true);
    disconnect(m_retryBtn, nullptr, this, nullptr);
    connect(m_retryBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, "Locate jackify-engine", QDir::homePath());
        if (path.isEmpty()) return;
        AppConfig::instance().setJackifyEnginePath(path);
        AppConfig::instance().save();
        if (WabbajackEngine::available()) {
            // restore normal Retry behaviour and fetch
            m_search->setEnabled(true);
            m_refreshBtn->setEnabled(true);
            m_list->setVisible(true);
            m_retryBtn->setText("Retry");
            disconnect(m_retryBtn, nullptr, this, nullptr);
            connect(m_retryBtn, &QPushButton::clicked, this, &WabbajackDialog::startFetch);
            startFetch();
        } else {
            QMessageBox::warning(this, "jackify-engine",
                "That file doesn't appear to be a usable jackify-engine.");
        }
    });
}

void WabbajackDialog::startFetch() {
    m_all.clear();
    m_filtered.clear();
    m_list->clear();
    ++m_thumbGen; // invalidate any in-flight thumbnails
    m_retryBtn->setVisible(false);
    m_statusLabel->setText("Loading gallery\xe2\x80\xa6");
    m_refreshBtn->setEnabled(false);
    m_engine->fetchModlists();
}

void WabbajackDialog::onModlistsReady(const QList<WabbajackModlist>& modlists) {
    m_all = modlists;
    m_refreshBtn->setEnabled(true);
    m_statusLabel->clear();
    m_selectedTags.clear();
    rebuildTagMenu();
    applyFilter();
}

QStringList WabbajackDialog::collectTags(const QList<WabbajackModlist>& lists) {
    QSet<QString> set;
    for (const auto& ml : lists)
        for (const QString& t : ml.tags)
            if (!t.trimmed().isEmpty()) set.insert(t.trimmed());
    QStringList out(set.cbegin(), set.cend());
    out.sort(Qt::CaseInsensitive);
    return out;
}

void WabbajackDialog::rebuildTagMenu() {
    if (!m_tagMenu) return;
    m_tagMenu->clear();

    // "Any tag" clears the selection.
    QAction* anyAct = m_tagMenu->addAction("Any tag");
    connect(anyAct, &QAction::triggered, this, [this] {
        m_selectedTags.clear();
        rebuildTagMenu();
        applyFilter();
    });
    m_tagMenu->addSeparator();

    const QStringList tags = collectTags(m_all);
    for (const QString& tag : tags) {
        QAction* act = m_tagMenu->addAction(tag);
        act->setCheckable(true);
        act->setChecked(m_selectedTags.contains(tag));
        connect(act, &QAction::toggled, this, [this, tag](bool on) {
            if (on) m_selectedTags.insert(tag);
            else    m_selectedTags.remove(tag);
            updateTagButtonLabel();
            applyFilter();
        });
    }
    updateTagButtonLabel();
}

void WabbajackDialog::updateTagButtonLabel() {
    if (!m_tagBtn) return;
    if (m_selectedTags.isEmpty()) m_tagBtn->setText("Tags");
    else m_tagBtn->setText(QStringLiteral("Tags (%1)").arg(m_selectedTags.size()));
}

bool WabbajackDialog::passesFilters(const WabbajackModlist& ml, const QString& searchQ,
                                    OfficialFilter official,
                                    const QSet<QString>& selectedTags) {
    // Official / Unofficial.
    if (official == OfficialFilter::Official && !ml.official) return false;
    if (official == OfficialFilter::Unofficial && ml.official) return false;

    // Tag filter: list must contain all selected tags (composable and). Empty = any.
    if (!selectedTags.isEmpty()) {
        const QSet<QString> listTags(ml.tags.cbegin(), ml.tags.cend());
        for (const QString& t : selectedTags)
            if (!listTags.contains(t)) return false;
    }

    // Search text over title/author/description.
    const QString q = searchQ.trimmed();
    if (!q.isEmpty()) {
        const bool match =
            ml.title.contains(q, Qt::CaseInsensitive) ||
            ml.author.contains(q, Qt::CaseInsensitive) ||
            ml.description.contains(q, Qt::CaseInsensitive);
        if (!match) return false;
    }
    return true;
}

void WabbajackDialog::onFailed(const QString& error) {
    m_refreshBtn->setEnabled(true);
    if (m_installing) {
        // A fetch shouldn't be running during install, but be safe.
        onLogLine("ERROR: " + error);
        return;
    }
    m_statusLabel->setText("Failed to load gallery: " + error);
    m_retryBtn->setText("Retry");
    m_retryBtn->setVisible(true);
}

void WabbajackDialog::applyFilter() {
    const QString q = m_search->text().trimmed();
    // Solero targets Skyrim SE only - the gallery is fixed to it.
    static const QString kGame = QStringLiteral("Skyrim Special Edition");
    const auto official = m_officialCombo
        ? OfficialFilter(m_officialCombo->currentData().toInt())
        : OfficialFilter::All;

    m_filtered.clear();
    ++m_thumbGen; // late thumbnail replies for the old view must not apply
    const int gen = m_thumbGen;
    m_list->clear();

    for (const auto& ml : m_all) {
        if (ml.gameHuman != kGame) continue;
        if (!passesFilters(ml, q, official, m_selectedTags)) continue;
        m_filtered << ml;
    }

    for (const auto& ml : m_filtered) {
        QString subtitle = ml.author;
        if (!ml.gameHuman.isEmpty()) subtitle += "  \xe2\x80\xa2  " + ml.gameHuman;
        QString sizes;
        if (!ml.downloadSizeStr.isEmpty() || !ml.installSizeStr.isEmpty())
            sizes = QString("  \xe2\x80\xa2  \xe2\xac\x87 %1 / %2")
                        .arg(ml.downloadSizeStr, ml.installSizeStr);
        QStringList tags;
        if (ml.official) tags << "Official";
        if (ml.utility) tags << "Utility/Test";
        if (ml.nsfw) tags << "NSFW";
        QString tagStr = tags.isEmpty() ? QString() : ("  [" + tags.join(", ") + "]");

        auto* item = new QListWidgetItem(
            ml.title + "\n" + subtitle + sizes + tagStr, m_list);
        item->setIcon(QIcon()); // placeholder until thumb loads
        if (!ml.imageUrl.isEmpty())
            loadThumb(item, ml.imageUrl);
    }

    if (m_filtered.isEmpty() && !m_all.isEmpty())
        m_statusLabel->setText("No modlists match the current filter.");
    else if (!m_all.isEmpty())
        m_statusLabel->clear();

    onSelectionChanged();
    Q_UNUSED(gen);
}

QString WabbajackDialog::thumbCachePath(const QString& url) {
    static const QString dir = [] {
        const QString d = AppConfig::dataRoot() + "/wabbajack-thumbs";
        QDir().mkpath(d);
        return d;
    }();
    const QString h = QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex());
    return dir + "/" + h + ".png";
}

void WabbajackDialog::loadThumb(QListWidgetItem* item, const QString& url) {
    // Capture the row index, not the pointer: a re-filter both bumps m_thumbGen
    // and rebuilds the list, so checking the generation guards against any stale
    // apply, and item() re-resolves the (same-generation) row safely.
    const int gen = m_thumbGen;
    const int row = m_list->row(item);

    // 1) In-memory cache -> instant (re-filter / re-open within the session).
    auto memIt = m_thumbMem.constFind(url);
    if (memIt != m_thumbMem.constEnd()) { item->setIcon(QIcon(memIt.value())); return; }

    // 2) On-disk cache -> instant + offline, no re-download across sessions.
    const QString path = thumbCachePath(url);
    if (QFileInfo::exists(path)) {
        QPixmap pm(path);
        if (!pm.isNull()) { m_thumbMem.insert(url, pm); item->setIcon(QIcon(pm)); return; }
    }

    // 3) Network -> downscale to a compact thumbnail, cache to disk + memory.
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, row, gen, url, path] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        if (!pm.loadFromData(reply->readAll()) || pm.isNull()) return;
        // The list icon is 80×45; store a 160-wide thumb (crisp, tiny on disk).
        const QPixmap thumb = pm.width() > 160
            ? pm.scaledToWidth(160, Qt::SmoothTransformation) : pm;
        thumb.save(path, "PNG");          // cache regardless of current filter
        m_thumbMem.insert(url, thumb);
        if (gen != m_thumbGen) return;     // list re-filtered: don't apply to a stale row
        if (QListWidgetItem* it = m_list->item(row)) it->setIcon(QIcon(thumb));
    });
}

void WabbajackDialog::onSelectionChanged() {
    int row = m_list->currentRow();
    if (row < 0 || row >= m_filtered.size()) {
        m_detailImage->clear();
        m_detailImage->setText("Select a modlist");
        m_detailTitle->clear();
        m_detailMeta->clear();
        m_detailDesc->clear();
        m_installBtn->setEnabled(false);
        m_readmeBtn->setEnabled(false);
        return;
    }
    const auto& ml = m_filtered.at(row);
    m_detailTitle->setText(ml.title);
    QStringList meta;
    if (!ml.author.isEmpty())   meta << "by " + ml.author;
    if (!ml.gameHuman.isEmpty())meta << ml.gameHuman;
    if (!ml.version.isEmpty())  meta << "v" + ml.version;
    QString sizeLine;
    if (!ml.downloadSizeStr.isEmpty() || !ml.installSizeStr.isEmpty())
        sizeLine = QString("Download %1  \xe2\x80\xa2  Install %2")
                       .arg(ml.downloadSizeStr, ml.installSizeStr);
    m_detailMeta->setText(meta.join("  \xe2\x80\xa2  ") + (sizeLine.isEmpty() ? "" : "\n" + sizeLine));
    m_detailDesc->setPlainText(ml.description);
    m_readmeBtn->setEnabled(!ml.readmeUrl.isEmpty());
    m_installBtn->setEnabled(true);

    // Larger detail image (async, generation-guarded via thumbnail mechanism is
    // separate; use a simple one-shot here keyed on current row's url).
    m_detailImage->setText("");
    if (!ml.imageUrl.isEmpty()) {
        const int gen = m_thumbGen;
        const QString wantUrl = ml.imageUrl;
        QNetworkReply* reply = m_net->get(QNetworkRequest(QUrl(ml.imageUrl)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, gen, wantUrl] {
            reply->deleteLater();
            if (gen != m_thumbGen) return;
            // ensure the still-selected item is the one we fetched for
            int r = m_list->currentRow();
            if (r < 0 || r >= m_filtered.size() || m_filtered.at(r).imageUrl != wantUrl) return;
            if (reply->error() != QNetworkReply::NoError) return;
            QPixmap pm;
            if (pm.loadFromData(reply->readAll()) && !pm.isNull())
                m_detailImage->setPixmap(pm.scaled(m_detailImage->size(),
                    Qt::KeepAspectRatio, Qt::SmoothTransformation));
        });
    } else {
        m_detailImage->setText("(no image)");
    }
}

void WabbajackDialog::triggerInstall(const QString& target, bool isLocalFile,
                                     const QString& displayName) {
    if (!NexusApi::keyAvailable()) {
        QMessageBox::warning(this, "Nexus sign-in required",
            "Installing a Wabbajack modlist downloads mods from Nexus.\n\n"
            "Sign in first via Settings \xe2\x86\x92 Nexus Account, then try again.");
        return;
    }

    // Ask for install + downloads dirs
    QDialog dirDlg(this);
    dirDlg.setWindowTitle("Install location");
    auto* dv = new QVBoxLayout(&dirDlg);
    auto* form = new QFormLayout;

    // displayName is already the file's base name for a local .wabbajack file
    // (see the file-picker call site) and the modlist title for a gallery entry.
    const QString defName = sanitize(displayName);
    QString defInstall = QDir::homePath() + "/Modding/Solero/Wabbajack/" +
                         (defName.isEmpty() ? "modlist" : defName);

    auto* installEdit = new QLineEdit(defInstall, &dirDlg);
    auto* installBrowse = new QPushButton("Browse\xe2\x80\xa6", &dirDlg);
    auto* installRow = new QHBoxLayout;
    installRow->addWidget(installEdit, 1);
    installRow->addWidget(installBrowse);
    auto* installWrap = new QWidget(&dirDlg);
    installWrap->setLayout(installRow);
    form->addRow("Install directory", installWrap);

    QString defDownloads = AppConfig::instance().downloadsDir();
    if (defDownloads.isEmpty())
        defDownloads = QDir::homePath() + "/Modding/Solero/downloads";
    auto* dlEdit = new QLineEdit(defDownloads, &dirDlg);
    auto* dlBrowse = new QPushButton("Browse\xe2\x80\xa6", &dirDlg);
    auto* dlRow = new QHBoxLayout;
    dlRow->addWidget(dlEdit, 1);
    dlRow->addWidget(dlBrowse);
    auto* dlWrap = new QWidget(&dirDlg);
    dlWrap->setLayout(dlRow);
    form->addRow("Downloads directory", dlWrap);

    dv->addLayout(form);

    auto* note = new QLabel(
        "Requires Skyrim Special Edition installed via Steam. This downloads many "
        "GB and can take a long time.", &dirDlg);
    note->setWordWrap(true);
    note->setStyleSheet("color:#aaa;");
    dv->addWidget(note);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dirDlg);
    dv->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dirDlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dirDlg, &QDialog::reject);
    connect(installBrowse, &QPushButton::clicked, &dirDlg, [&] {
        const QString d = QFileDialog::getExistingDirectory(&dirDlg, "Install directory",
                                                            installEdit->text());
        if (!d.isEmpty()) installEdit->setText(d);
    });
    connect(dlBrowse, &QPushButton::clicked, &dirDlg, [&] {
        const QString d = QFileDialog::getExistingDirectory(&dirDlg, "Downloads directory",
                                                            dlEdit->text());
        if (!d.isEmpty()) dlEdit->setText(d);
    });

    if (dirDlg.exec() != QDialog::Accepted) return;

    const QString installDir = installEdit->text().trimmed();
    const QString downloadsDir = dlEdit->text().trimmed();
    if (installDir.isEmpty() || downloadsDir.isEmpty()) {
        QMessageBox::warning(this, "Install", "Both directories are required.");
        return;
    }
    if (!QDir().mkpath(installDir) || !QDir().mkpath(downloadsDir)) {
        QMessageBox::warning(this, "Install",
            "Could not create the install/downloads directories.");
        return;
    }

    // Remember everything needed to retry/resume on failure.
    m_installDir = installDir;
    m_installTitle = displayName;
    m_installTarget = target;
    m_installIsLocalFile = isLocalFile;
    m_installDownloadsDir = downloadsDir;

    startInstallRun();
}

void WabbajackDialog::startInstallRun() {
    // Switch to the progress page and kick off (or resume) the install using the
    // remembered parameters. The engine resumes from the downloads dir.
    m_installing = true;
    m_progTitle->setText("Installing: " + m_installTitle);
    m_progBar->setRange(0, 0); // indeterminate until first pct arrives
    m_progBar->setValue(0);
    m_progOp->setText("Starting\xe2\x80\xa6");
    m_progLog->clear();
    m_backBtn->setVisible(false);
    if (m_retryInstallBtn) m_retryInstallBtn->setVisible(false);
    m_cancelBtn->setEnabled(true);
    m_stack->setCurrentWidget(m_progressPage);

    m_engine->install(m_installTarget, m_installIsLocalFile,
                      m_installDir, m_installDownloadsDir);
}

void WabbajackDialog::onProgress(const QString& op, const QString& file, double pct) {
    if (pct >= 0.0) {
        if (m_progBar->maximum() == 0) m_progBar->setRange(0, 100);
        m_progBar->setValue(qBound(0, int(pct + 0.5), 100));
    }
    QString line = op;
    if (!file.isEmpty()) line += ": " + file;
    m_progOp->setText(line);
}

void WabbajackDialog::onLogLine(const QString& line) {
    m_progLog->appendPlainText(line);
}

void WabbajackDialog::onInstallFinished(bool ok, int exitCode) {
    m_installing = false;
    m_cancelBtn->setEnabled(false);
    m_progBar->setRange(0, 100);

    if (!ok) {
        // The classified report is shown from onInstallFailed (always emitted
        // alongside this signal on failure); just surface the log + controls here.
        m_progLog->appendPlainText(
            QString("\n--- Install failed (exit code %1) ---").arg(exitCode));
        m_progBar->setValue(0);
        m_backBtn->setVisible(true);
        if (m_retryInstallBtn) m_retryInstallBtn->setVisible(true);
        return;
    }

    m_progBar->setValue(100);
    m_progOp->setText("Install complete - importing as a Solero profile\xe2\x80\xa6");
    doImport();
}

void WabbajackDialog::onInstallFailed(int exitCode, const QList<FailedArchive>& failed) {
    if (failed.isEmpty()) {
        // Unknown failure - fall back to the bare exit-code message.
        QMessageBox::warning(this, "Install failed",
            QString("Install failed (exit code %1) - see log.\n\n"
                    "You can Retry / Resume to continue from where it left off.")
                .arg(exitCode));
        return;
    }
    showFailureReport(exitCode, failed);
}

void WabbajackDialog::showFailureReport(int exitCode,
                                        const QList<FailedArchive>& failed) {
    // Partition by group.
    QList<FailedArchive> ck;       // Creation-Kit tool files (GameFileSource)
    QList<FailedArchive> gameData; // Missing game content / AE / Creation Club files
    QList<FailedArchive> manual;   // Mega / WabbajackCDN / Http / Nexus / Other
    for (const auto& fa : failed) {
        if (fa.source == FailedSource::GameFileSource) {
            // Files under the game's Data/ folder (or plugin/archive extensions)
            // are missing GAME CONTENT (AE / Creation Club), not CK tools.
            const QString p = fa.path;
            const bool isGameData =
                p.contains(QStringLiteral("/Data/")) ||
                p.contains(QStringLiteral("\\Data\\")) ||
                p.endsWith(QStringLiteral(".esl"), Qt::CaseInsensitive) ||
                p.endsWith(QStringLiteral(".esm"), Qt::CaseInsensitive) ||
                p.endsWith(QStringLiteral(".esp"), Qt::CaseInsensitive) ||
                p.endsWith(QStringLiteral(".bsa"), Qt::CaseInsensitive);
            if (isGameData) gameData << fa;
            else            ck << fa;
        } else {
            manual << fa;
        }
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Some downloads couldn't be completed");
    dlg.resize(620, 560);
    auto* outer = new QVBoxLayout(&dlg);

    auto* intro = new QLabel(
        QString("This modlist couldn't finish installing - %1 file%2 "
                "could not be downloaded. The engine can't skip required files, "
                "so resolve the items below, then Retry / Resume.")
            .arg(failed.size()).arg(failed.size() == 1 ? "" : "s"), &dlg);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    // Scrollable body so long file lists don't blow up the dialog.
    auto* scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    auto* body = new QWidget(scroll);
    auto* bodyL = new QVBoxLayout(body);

    // Missing game content (AE / Creation Club) group
    if (!gameData.isEmpty()) {
        QStringList names;
        for (const auto& fa : gameData) names << fa.name;

        auto* box = new QGroupBox("Missing game files", body);
        auto* gl = new QVBoxLayout(box);

        auto* heading = new QLabel(box);
        heading->setWordWrap(true);
        heading->setStyleSheet("font-weight:bold;");
        heading->setText(QString(
            "Your Skyrim install is missing required game files: %1.")
            .arg(names.join(", ")));
        gl->addWidget(heading);

        auto* detail = new QLabel(QString(
            "These are Anniversary Edition / Creation Club files that must be "
            "present in your Skyrim Data folder. In Steam, verify/repair Skyrim "
            "Special Edition (and ensure AE content is installed), then Retry.\n\n"
            "Note: Solero auto-links lowercase copies of Creation Club files "
            "before each install to handle Linux case-sensitivity; if this still "
            "fails, the content is genuinely absent."), box);
        detail->setWordWrap(true);
        detail->setStyleSheet("color:#aaa;");
        gl->addWidget(detail);

        bodyL->addWidget(box);
    }

    // Creation Kit group
    if (!ck.isEmpty()) {
        const QString version = ck.first().version;
        const QString gameDir = AppConfig::instance().gameDir();
        const bool ckInstalled =
            !gameDir.isEmpty() &&
            QFileInfo::exists(gameDir + "/CreationKit.exe");

        auto* box = new QGroupBox("Creation Kit files", body);
        auto* gl = new QVBoxLayout(box);

        auto* heading = new QLabel(box);
        heading->setWordWrap(true);
        heading->setStyleSheet("font-weight:bold;");
        if (ckInstalled) {
            heading->setText(QString(
                "Creation Kit is installed but version-mismatched - "
                "these %1 file%2 must be placed manually.")
                .arg(ck.size()).arg(ck.size() == 1 ? "" : "s"));
        } else {
            heading->setText(QString(
                "This list needs the Creation Kit - %1 file%2 missing%3.")
                .arg(ck.size())
                .arg(ck.size() == 1 ? "" : "s")
                .arg(version.isEmpty() ? QString()
                                       : QString(" (game v%1)").arg(version)));
        }
        gl->addWidget(heading);

        if (ckInstalled) {
            // Show the file list so the user knows what to place.
            auto* list = new QListWidget(box);
            list->setMaximumHeight(180);
            for (const auto& fa : ck) {
                const QString label = fa.path.isEmpty() ? fa.name
                                        : fa.name + "  (" + fa.path + ")";
                list->addItem(label);
            }
            gl->addWidget(list);
        } else {
            auto* caveat = new QLabel(QString(
                "Install the Creation Kit from Steam, then Retry. Note: this list "
                "is pinned to a specific game version%1, so a newer CK build may "
                "still mismatch - you may need to place these files manually.")
                .arg(version.isEmpty() ? QString()
                                       : QString(" (v%1)").arg(version)), box);
            caveat->setWordWrap(true);
            caveat->setStyleSheet("color:#aaa;");
            gl->addWidget(caveat);

            auto* btn = new QPushButton("Install Creation Kit", box);
            connect(btn, &QPushButton::clicked, &dlg, [] {
                QDesktopServices::openUrl(QUrl("steam://install/1946180"));
            });
            auto* btnRow = new QHBoxLayout;
            btnRow->addWidget(btn);
            btnRow->addStretch();
            gl->addLayout(btnRow);
        }
        bodyL->addWidget(box);
    }

    // Manual-download group
    if (!manual.isEmpty()) {
        QString downloadsDir = m_installDownloadsDir;
        if (downloadsDir.isEmpty()) downloadsDir = AppConfig::instance().downloadsDir();

        auto* box = new QGroupBox("Manual downloads", body);
        auto* gl = new QVBoxLayout(box);

        auto* heading = new QLabel(QString(
            "%1 file%2 must be downloaded manually, then placed in the downloads "
            "folder:\n%3")
            .arg(manual.size()).arg(manual.size() == 1 ? "" : "s")
            .arg(downloadsDir.isEmpty() ? "(downloads directory)" : downloadsDir),
            box);
        heading->setWordWrap(true);
        gl->addWidget(heading);

        for (const auto& fa : manual) {
            auto* row = new QHBoxLayout;
            auto* nameLbl = new QLabel(fa.name, box);
            nameLbl->setWordWrap(true);
            row->addWidget(nameLbl, 1);
            if (!fa.url.isEmpty()) {
                auto* open = new QPushButton("Open", box);
                const QString url = fa.url;
                connect(open, &QPushButton::clicked, &dlg, [url] {
                    QDesktopServices::openUrl(QUrl(url));
                });
                row->addWidget(open);
            }
            gl->addLayout(row);
        }
        bodyL->addWidget(box);
    }

    bodyL->addStretch();
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    auto* bb = new QDialogButtonBox(&dlg);
    auto* retryBtn = bb->addButton("Retry / Resume", QDialogButtonBox::AcceptRole);
    bb->addButton(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(retryBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    outer->addWidget(bb);

    if (dlg.exec() == QDialog::Accepted && !m_installTarget.isEmpty())
        startInstallRun();
}

void WabbajackDialog::doImport() {
    const QString profilesDir = m_installDir + "/profiles";

    // Require only profiles/ - Mo2Importer tolerates a missing mods/ dir (a valid
    // install can have zero staged mods), so don't hard-gate on mods/ existing.
    if (!QDir(profilesDir).exists()) {
        m_backBtn->setVisible(true);
        QMessageBox::warning(this, "Import",
            "Install completed but the expected MO2 layout wasn't found at\n" + m_installDir);
        return;
    }

    // Import every MO2 profile in the instance as its own Solero profile, sharing
    // the staged mods so they aren't duplicated. Pass the RAW title so the
    // title-separator filter can match the WJ header (which keeps its real
    // apostrophe); importInstance sanitizes it internally for profile naming.
    const QString rawTitle = m_installTitle.trimmed();
    const QString listTitle = sanitize(m_installTitle);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto r = Mo2Importer::importInstance(m_installDir,
        AppConfig::instance().stagingDir(), *m_profiles,
        rawTitle.isEmpty() ? QStringLiteral("Wabbajack") : rawTitle,
        /*symlinkMods=*/true);
    QApplication::restoreOverrideCursor();

    if (!r.success) {
        // importInstance is responsible for removing any profiles it created when
        // it fails (e.g. the zero-mod case), and clears r.profileNames accordingly,
        // so there is nothing to clean up here - do not add a second delete loop.
        m_backBtn->setVisible(true);
        QMessageBox::critical(this, "Import Failed",
            r.errorMessage.isEmpty() ? "Unknown error importing the installed modlist." : r.errorMessage);
        return;
    }

    const QString titleForMsg = listTitle.isEmpty() ? m_installTitle : listTitle;
    QMessageBox::information(this, "Imported",
        QString("Imported '%1' - %2 profile%3 (%4), %5 mods.\n\n"
                "Mods are symlinked from %6 - keep that folder.")
            .arg(titleForMsg)
            .arg(r.profileNames.size())
            .arg(r.profileNames.size() == 1 ? "" : "s")
            .arg(r.profileNames.join(", "))
            .arg(r.modsStaged)
            .arg(m_installDir));
    emit profileImported(r.primaryProfile, r.tools);
    accept();
}

bool WabbajackDialog::confirmCloseWhileInstalling() {
    if (!m_installing) return true;
    if (QMessageBox::question(this, "Cancel install",
            "Cancel the in-progress install? Partially downloaded files are kept.")
        != QMessageBox::Yes) {
        return false;
    }
    m_engine->cancel();
    return true;
}

void WabbajackDialog::closeEvent(QCloseEvent* e) {
    if (!confirmCloseWhileInstalling()) {
        e->ignore();
        return;
    }
    QDialog::closeEvent(e);
}

void WabbajackDialog::reject() {
    if (!confirmCloseWhileInstalling()) return;
    QDialog::reject();
}

QString WabbajackDialog::sanitize(const QString& s) {
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (c.isLetterOrNumber() || c == ' ' || c == '-' || c == '_')
            out += c;
        else
            out += ' ';
    }
    return out.simplified();
}

} // namespace solero
