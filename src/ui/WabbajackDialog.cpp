#include "WabbajackDialog.h"
#include "wabbajack/WabbajackEngine.h"
#include "import/Mo2Importer.h"
#include "nexus/NexusApi.h"
#include "core/AppConfig.h"

#include <QStackedWidget>
#include <QLineEdit>
#include <QComboBox>
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
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
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

    m_gameFilter = new QComboBox(m_galleryPage);
    topBar->addWidget(m_gameFilter);

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
    connect(m_gameFilter, &QComboBox::currentTextChanged, this, &WabbajackDialog::applyFilter);
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
    m_gameFilter->setEnabled(false);
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
            m_gameFilter->setEnabled(true);
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
    rebuildGameFilter();
    applyFilter();
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

void WabbajackDialog::rebuildGameFilter() {
    QSignalBlocker block(m_gameFilter);
    const QString prev = m_gameFilter->currentText();
    m_gameFilter->clear();
    m_gameFilter->addItem("All games");
    QStringList games;
    for (const auto& ml : m_all)
        if (!ml.gameHuman.isEmpty() && !games.contains(ml.gameHuman))
            games << ml.gameHuman;
    games.sort(Qt::CaseInsensitive);
    m_gameFilter->addItems(games);

    // Default to Skyrim SE on first population, else keep the prior choice.
    QString target = prev.isEmpty() ? QStringLiteral("Skyrim Special Edition") : prev;
    int idx = m_gameFilter->findText(target, Qt::MatchFixedString);
    m_gameFilter->setCurrentIndex(idx >= 0 ? idx : 0);
}

void WabbajackDialog::applyFilter() {
    const QString q = m_search->text().trimmed();
    const QString game = m_gameFilter->currentText();
    const bool allGames = (game.isEmpty() || game == "All games");

    m_filtered.clear();
    ++m_thumbGen; // late thumbnail replies for the old view must not apply
    const int gen = m_thumbGen;
    m_list->clear();

    for (const auto& ml : m_all) {
        if (!allGames && ml.gameHuman != game) continue;
        if (!q.isEmpty()) {
            const bool match =
                ml.title.contains(q, Qt::CaseInsensitive) ||
                ml.author.contains(q, Qt::CaseInsensitive) ||
                ml.description.contains(q, Qt::CaseInsensitive);
            if (!match) continue;
        }
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

void WabbajackDialog::loadThumb(QListWidgetItem* item, const QString& url) {
    // Capture the row index, not the pointer: a re-filter both bumps m_thumbGen
    // and rebuilds the list, so checking the generation guards against any stale
    // apply, and item() re-resolves the (same-generation) row safely.
    const int gen = m_thumbGen;
    const int row = m_list->row(item);
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, row, gen] {
        reply->deleteLater();
        if (gen != m_thumbGen) return;            // list re-filtered: stale
        if (reply->error() != QNetworkReply::NoError) return;
        QListWidgetItem* it = m_list->item(row);
        if (!it) return;
        QPixmap pm;
        if (pm.loadFromData(reply->readAll()) && !pm.isNull())
            it->setIcon(QIcon(pm));
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

    // Switch to the progress page and kick off the install.
    m_installDir = installDir;
    m_installTitle = displayName;
    m_installing = true;
    m_progTitle->setText("Installing: " + displayName);
    m_progBar->setRange(0, 0); // indeterminate until first pct arrives
    m_progBar->setValue(0);
    m_progOp->setText("Starting\xe2\x80\xa6");
    m_progLog->clear();
    m_backBtn->setVisible(false);
    m_cancelBtn->setEnabled(true);
    m_stack->setCurrentWidget(m_progressPage);

    m_engine->install(target, isLocalFile, installDir, downloadsDir);
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
        m_progLog->appendPlainText(
            QString("\n--- Install failed (exit code %1) ---").arg(exitCode));
        m_progBar->setValue(0);
        m_backBtn->setVisible(true);
        QMessageBox::warning(this, "Install failed",
            QString("Install failed (exit code %1) - see log.").arg(exitCode));
        return;
    }

    m_progBar->setValue(100);
    m_progOp->setText("Install complete - importing as a Solero profile\xe2\x80\xa6");
    doImport();
}

void WabbajackDialog::doImport() {
    const QString profilesDir = m_installDir + "/profiles";
    const QString modsDir = m_installDir + "/mods";

    if (!QDir(profilesDir).exists() || !QDir(modsDir).exists()) {
        m_backBtn->setVisible(true);
        QMessageBox::warning(this, "Import",
            "Install completed but the expected MO2 layout wasn't found at\n" + m_installDir);
        return;
    }

    // Determine the profile name: ModOrganizer.ini selected_profile, else first subdir.
    QString profileName;
    const QString iniPath = m_installDir + "/ModOrganizer.ini";
    if (QFileInfo::exists(iniPath)) {
        QSettings ini(iniPath, QSettings::IniFormat);
        ini.beginGroup("General");
        profileName = ini.value("selected_profile").toString();
        ini.endGroup();
        // MO2 sometimes wraps values in @ByteArray(...) / quotes; trim quotes.
        profileName.remove('"');
    }
    if (profileName.isEmpty()) {
        const auto subs = QDir(profilesDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (!subs.isEmpty()) profileName = subs.first();
    }
    if (profileName.isEmpty()) {
        m_backBtn->setVisible(true);
        QMessageBox::warning(this, "Import",
            "Install completed but no MO2 profile was found under\n" + profilesDir);
        return;
    }

    const QString mo2ProfileDir = profilesDir + "/" + profileName;
    if (!QDir(mo2ProfileDir).exists()) {
        m_backBtn->setVisible(true);
        QMessageBox::warning(this, "Import",
            "Install completed but the profile folder wasn't found at\n" + mo2ProfileDir);
        return;
    }

    const QString newName = sanitize(m_installTitle);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto r = Mo2Importer::importProfile(mo2ProfileDir, modsDir,
        AppConfig::instance().stagingDir(), *m_profiles,
        newName.isEmpty() ? QStringLiteral("Wabbajack") : newName, /*symlinkMods=*/true);
    QApplication::restoreOverrideCursor();

    if (!r.success) {
        m_backBtn->setVisible(true);
        QMessageBox::critical(this, "Import Failed",
            r.errorMessage.isEmpty() ? "Unknown error importing the installed modlist." : r.errorMessage);
        return;
    }

    QMessageBox::information(this, "Imported",
        QString("Imported '%1' - %2 mods.\n\n"
                "Mods are symlinked from %3 - keep that folder.")
            .arg(r.profileName).arg(r.modsStaged).arg(m_installDir));
    emit profileImported(r.profileName);
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
